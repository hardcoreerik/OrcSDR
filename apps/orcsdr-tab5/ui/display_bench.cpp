#include "display_bench.hpp"

#include <M5Unified.h>
#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "display_rotate.hpp"
#include "dsp_stats.hpp"
#include "screen_blit.hpp"

namespace orcsdr::display_bench {
namespace {

// The Home trace sprite: kPlotW - 2 by kSpectrumH - 2 at (kPlotX + 1, kSpectrumY + 1) in home_dashboard.cpp.
constexpr int kX0 = 331, kY0 = 113, kW = 904, kH = 184;

void say(Emit emit, const char* format, ...) {
  char line[640];
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  emit(line);
}

struct Timing {
  uint32_t frames = 0;
  uint64_t total_us = 0;
  uint32_t max_us = 0;
};

template <typename Frame>
Timing timed_loop(unsigned seconds, Frame&& frame) {
  Timing t;
  const int64_t end = esp_timer_get_time() + static_cast<int64_t>(seconds) * 1000000;
  while (esp_timer_get_time() < end) {
    const int64_t started = esp_timer_get_time();
    frame();
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - started);
    ++t.frames;
    t.total_us += us;
    if (us > t.max_us) t.max_us = us;
    (void)esp_task_wdt_reset();  // the main loop is ours for the whole run
    taskYIELD();
  }
  return t;
}

void reset_dsp_window() {
  char discard[512];
  (void)dsp::stats::format_and_reset(discard, sizeof(discard), 1000);
}

void report(Emit emit, const char* method, const Timing& t, unsigned seconds) {
  say(emit, "RTL_DISPLAY_BENCH method=%s frames=%lu avg_us=%lu max_us=%lu", method,
      static_cast<unsigned long>(t.frames),
      static_cast<unsigned long>(t.frames ? t.total_us / t.frames : 0), static_cast<unsigned long>(t.max_us));
  char dsp[512];
  (void)dsp::stats::format_and_reset(dsp, sizeof(dsp), seconds * 1000u);
  const char* body = std::strncmp(dsp, "RTL_DSP_STATS ", 14) == 0 ? dsp + 14 : dsp;
  say(emit, "RTL_DISPLAY_BENCH_DSP method=%s %s", method, body);
}

// Rows of the framebuffer that hold the region, written back from the cache so the panel (and the PPA) see them.
void write_back(uint16_t* fb, const display_rotate::Geometry& g, const display_rotate::Rect& r) {
  uint16_t* start = fb + static_cast<size_t>(r.y) * g.panel_w;
  const size_t bytes = static_cast<size_t>(r.h) * g.panel_w * sizeof(uint16_t);
  (void)esp_cache_msync(start, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

void capture(const uint16_t* fb, const display_rotate::Geometry& g, const display_rotate::Rect& r, uint16_t* out) {
  for (int y = 0; y < r.h; ++y)
    std::memcpy(out + static_cast<size_t>(y) * r.w, fb + static_cast<size_t>(r.y + y) * g.panel_w + r.x,
                static_cast<size_t>(r.w) * sizeof(uint16_t));
}

void clear(uint16_t* fb, const display_rotate::Geometry& g, const display_rotate::Rect& r) {
  for (int y = 0; y < r.h; ++y) {
    uint16_t* row = fb + static_cast<size_t>(r.y + y) * g.panel_w + r.x;
    for (int x = 0; x < r.w; ++x) row[x] = 0xDEAD;
  }
  write_back(fb, g, r);
}

size_t mismatches(const uint16_t* fb, const display_rotate::Geometry& g, const display_rotate::Rect& r,
                  const uint16_t* want) {
  size_t wrong = 0;
  for (int y = 0; y < r.h; ++y) {
    const uint16_t* row = fb + static_cast<size_t>(r.y + y) * g.panel_w + r.x;
    const uint16_t* expect = want + static_cast<size_t>(y) * r.w;
    for (int x = 0; x < r.w; ++x) wrong += row[x] != expect[x];
  }
  return wrong;
}

const char* angle_name(ppa_srm_rotation_angle_t a) {
  switch (a) {
    case PPA_SRM_ROTATION_ANGLE_0: return "0";
    case PPA_SRM_ROTATION_ANGLE_90: return "90";
    case PPA_SRM_ROTATION_ANGLE_180: return "180";
    default: return "270";
  }
}

}  // namespace

void run(unsigned seconds, bool visualizer_active, Emit emit) {
  if (visualizer_active) {  // it page-flips through the second framebuffer
    emit("RTL_DISPLAY_BENCH_ERROR busy=visualizer");
    return;
  }
  auto* panel = static_cast<lgfx::Panel_DSI*>(M5.Display.getPanel());
  uint16_t* fb = panel ? static_cast<uint16_t*>(panel->getFrameBuffer(1)) : nullptr;
  if (fb == nullptr) {
    emit("RTL_DISPLAY_BENCH_ERROR no_back_buffer");
    return;
  }
  display_rotate::Geometry g;
  g.panel_w = panel->config().panel_width;
  g.panel_h = panel->config().panel_height;
  g.logical_w = M5.Display.width();
  g.logical_h = M5.Display.height();
  g.rotation = M5.Display.getRotation();
  const display_rotate::Rect r = display_rotate::panel_rect(g, kX0, kY0, kW, kH);
  const size_t fb_bytes = static_cast<size_t>(g.panel_w) * g.panel_h * sizeof(uint16_t);

  M5Canvas source;  // the same kind of sprite Home draws its trace into
  source.setPsram(true);
  source.setColorDepth(16);
  uint16_t* reference = static_cast<uint16_t*>(
      heap_caps_malloc(static_cast<size_t>(r.w) * r.h * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!source.createSprite(kW, kH) || reference == nullptr) {
    emit("RTL_DISPLAY_BENCH_ERROR no_memory");
    if (reference) heap_caps_free(reference);
    return;
  }
  uint16_t* pixels = static_cast<uint16_t*>(source.getBuffer());
  for (int y = 0; y < kH; ++y)  // every pixel distinct enough to catch a mirrored or shifted copy
    for (int x = 0; x < kW; ++x) pixels[static_cast<size_t>(y) * kW + x] = static_cast<uint16_t>(x * 131u + y * 17u + (x ^ y));

  M5Canvas target;  // the second framebuffer, seen through M5GFX with the screen's rotation (as rf_visualizer does)
  target.setColorDepth(lgfx::rgb565_nonswapped);
  target.setBuffer(fb, g.panel_w, g.panel_h);
  target.setSwapBytes(true);
  target.setRotation(g.rotation);

  const screen_blit::Counters live = screen_blit::counters();
  say(emit, "RTL_DISPLAY_BENCH_SCREEN_BLIT ppa=%lu fallback=%lu last_error=%s", live.ppa, live.fallback,
      live.last_error ? esp_err_to_name(live.last_error) : "none");
  say(emit, "RTL_DISPLAY_BENCH_BEGIN w=%d h=%d x=%d y=%d rotation=%u panel=%dx%d region=%d,%d,%dx%d seconds=%u", kW,
      kH, kX0, kY0, static_cast<unsigned>(g.rotation), g.panel_w, g.panel_h, r.x, r.y, r.w, r.h, seconds);

  // Reference picture: what M5GFX puts in the framebuffer today.
  clear(fb, g, r);
  source.pushSprite(&target, kX0, kY0);
  capture(fb, g, r, reference);

  // idle
  reset_dsp_window();
  const Timing idle = timed_loop(seconds, [] { vTaskDelay(pdMS_TO_TICKS(10)); });
  report(emit, "idle", idle, seconds);

  // m5gfx (per-pixel rotated copy), with the cache write-back a real screen update needs
  reset_dsp_window();
  const Timing m5 = timed_loop(seconds, [&] {
    source.pushSprite(&target, kX0, kY0);
    write_back(fb, g, r);
  });
  report(emit, "m5gfx", m5, seconds);

  // tiled CPU copy
  clear(fb, g, r);
  display_rotate::rotate_copy(g, pixels, kW, kW, kH, kX0, kY0, fb, true);
  say(emit, "RTL_DISPLAY_BENCH_CHECK method=tiled mismatches=%u", static_cast<unsigned>(mismatches(fb, g, r, reference)));
  reset_dsp_window();
  const Timing tiled = timed_loop(seconds, [&] {
    display_rotate::rotate_copy(g, pixels, kW, kW, kH, kX0, kY0, fb, true);
    write_back(fb, g, r);
  });
  report(emit, "tiled", tiled, seconds);

  // PPA
  ppa_client_handle_t client = nullptr;
  ppa_client_config_t client_config = {};
  client_config.oper_type = PPA_OPERATION_SRM;
  client_config.max_pending_trans_num = 1;
  client_config.data_burst_length = PPA_DATA_BURST_LENGTH_128;
  esp_err_t err = ppa_register_client(&client_config, &client);
  if (err != ESP_OK) {
    say(emit, "RTL_DISPLAY_BENCH_ERROR ppa_register=%s", esp_err_to_name(err));
  } else {
    ppa_srm_oper_config_t op = {};
    op.in.buffer = pixels;
    op.in.pic_w = kW;
    op.in.pic_h = kH;
    op.in.block_w = kW;
    op.in.block_h = kH;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.out.buffer = fb;
    op.out.buffer_size = fb_bytes;
    op.out.pic_w = g.panel_w;
    op.out.pic_h = g.panel_h;
    op.out.block_offset_x = r.x;
    op.out.block_offset_y = r.y;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.scale_x = 1.0f;
    op.scale_y = 1.0f;
    op.byte_swap = true;  // sprites hold byte-swapped RGB565; the framebuffer is native
    op.mode = PPA_TRANS_MODE_BLOCKING;
    bool found = false;
    const ppa_srm_rotation_angle_t angles[] = {PPA_SRM_ROTATION_ANGLE_90, PPA_SRM_ROTATION_ANGLE_270};
    for (ppa_srm_rotation_angle_t angle : angles) {
      for (int mirror = 0; mirror < 4 && !found; ++mirror) {
        op.rotation_angle = angle;
        op.mirror_x = (mirror & 1) != 0;
        op.mirror_y = (mirror & 2) != 0;
        clear(fb, g, r);
        err = ppa_do_scale_rotate_mirror(client, &op);
        const size_t wrong = err == ESP_OK ? mismatches(fb, g, r, reference) : static_cast<size_t>(-1);
        say(emit, "RTL_DISPLAY_BENCH_CHECK method=ppa angle=%s mirror_x=%d mirror_y=%d result=%s mismatches=%ld",
            angle_name(angle), op.mirror_x ? 1 : 0, op.mirror_y ? 1 : 0, esp_err_to_name(err),
            err == ESP_OK ? static_cast<long>(wrong) : -1L);
        if (err != ESP_OK) break;
        found = wrong == 0;
      }
      if (found || err != ESP_OK) break;
    }
    if (found) {
      // Each frame: the source sprite is written back by the driver, the PPA copies, and the CPU waits for it.
      reset_dsp_window();
      const Timing ppa = timed_loop(seconds, [&] { (void)ppa_do_scale_rotate_mirror(client, &op); });
      report(emit, "ppa", ppa, seconds);
      say(emit, "RTL_DISPLAY_BENCH_CHECK method=ppa_after_timing mismatches=%u",
          static_cast<unsigned>(mismatches(fb, g, r, reference)));
    } else {
      emit("RTL_DISPLAY_BENCH_ERROR ppa=no_exact_match");
    }
    // Both landscape rotations with the angle screen_blit uses (Settings can turn the screen over).
    for (const uint8_t rotation : {static_cast<uint8_t>(1), static_cast<uint8_t>(3)}) {
      display_rotate::Geometry gr = g;
      gr.rotation = rotation;
      const display_rotate::Rect rr = display_rotate::panel_rect(gr, kX0, kY0, kW, kH);
      const int degrees = display_rotate::ppa_ccw_degrees(rotation);
      target.setRotation(rotation);
      clear(fb, gr, rr);
      source.pushSprite(&target, kX0, kY0);
      capture(fb, gr, rr, reference);
      op.rotation_angle = degrees == 90 ? PPA_SRM_ROTATION_ANGLE_90 : PPA_SRM_ROTATION_ANGLE_270;
      op.mirror_x = false;
      op.mirror_y = false;
      op.out.block_offset_x = rr.x;
      op.out.block_offset_y = rr.y;
      clear(fb, gr, rr);
      err = ppa_do_scale_rotate_mirror(client, &op);
      say(emit, "RTL_DISPLAY_BENCH_CHECK method=screen_blit rotation=%u angle=%d result=%s mismatches=%ld",
          static_cast<unsigned>(rotation), degrees, esp_err_to_name(err),
          err == ESP_OK ? static_cast<long>(mismatches(fb, gr, rr, reference)) : -1L);
    }
    target.setRotation(g.rotation);
    (void)ppa_unregister_client(client);
  }

  source.deleteSprite();
  heap_caps_free(reference);
  emit("RTL_DISPLAY_BENCH_END");
}

}  // namespace orcsdr::display_bench

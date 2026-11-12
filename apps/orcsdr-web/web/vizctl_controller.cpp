// RF visualizer: OrcSDR's twelve-view signal display (spectrum, waterfall, phosphor, 3-D spectrum, constellation, IQ scope, polar,
// occupancy, peak/average, doppler, channelizer, audio spectrogram), fed by the node's raw IQ channel through the same web rf_analysis
// as RF Lab. On the Tab5 it draws straight into the DSI panel's two frame buffers; here the buffers are the stand-in panel's
// (stubs/lgfx/.../Panel_DSI.hpp) and every finished frame is converted into the page's image.
//
// The page opens a channel on the node and hands the IQ blocks over. The screen opens once the first block has arrived (it refuses to
// enter without a source, as on the device). What a browser cannot do is left off: there is no audio path to listen through, no SD card
// for captures, and the span is the channel's 300 kS/s.
#include <M5Unified.h>
#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "data_screens.hpp"
#include "esp_lcd_panel_ops.h"
#include "nvs_store.hpp"
#include "rf_analysis_web.hpp"
#include "rf_visualizer.hpp"

namespace {

using namespace orcsdr;
using namespace dscreens;

NvsStore g_nvs;
bool g_entered = false, g_inited = false, g_waiting = false, g_panel_used = false, g_failed = false;
uint32_t g_enter_ms = 0, g_last_iq_ms = 0, g_center_hz = 0;
char g_request[96];
bool g_request_pending = false;
double g_drops = 0;

constexpr uint32_t kRate = 300000;

visualizer::Runtime runtime() {
  visualizer::Runtime r;
  r.center_hz = g_center_hz;
  r.span_hz = kRate;
  r.sample_rate_sps = kRate;
  r.audio_rate_sps = 48000;
  r.consumer_drops = (uint32_t)g_drops;
  r.source_available = g_last_iq_ms && (uint32_t)emscripten_get_now() - g_last_iq_ms < 1500;
  r.sound_enabled = false;
  r.sd_writable = false;
  return r;
}

void ask_channel() {
  snprintf(g_request, sizeof g_request, "{\"op\":\"vizchan\",\"center\":%u}", (unsigned)g_center_hz);
  g_request_pending = true;
}

// The finished frame, little-endian RGB565 as the panel holds it, into the page's big-endian sprite.
void to_sprite(const void* frame) {
  auto* dst = static_cast<uint16_t*>(M5.Display.getBuffer());
  const auto* src = static_cast<const uint16_t*>(frame);
  if (!dst || !src) return;
  for (size_t i = 0; i < 1280u * 720u; ++i) dst[i] = (uint16_t)((src[i] << 8) | (src[i] >> 8));
}

void open_screen() {
  g_waiting = false;
  if (!g_inited) {
    g_nvs.begin("orcsdr");
    g_inited = visualizer::initialize(&g_nvs, nullptr);
  }
  visualizer::set_runtime(runtime());
  g_failed = !g_inited || !visualizer::enter(0, 0);
  if (g_failed) {
    M5.Display.fillScreen(0);
    M5.Display.setTextColor(0x8c71, 0);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("RF VISUALIZER NEEDS A RUNNING RECEIVER - TAP TO GO BACK", 640, 360);
  }
}

void enter(uint32_t now) {
  g_entered = true;
  g_waiting = true;
  g_failed = false;
  g_panel_used = false;
  g_enter_ms = now;
  g_last_iq_ms = 0;
  g_center_hz = 0;   // 0 = wherever the page is tuned
  M5.Display.fillScreen(0);
  M5.Display.setTextColor(0x8c71, 0);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("OPENING THE NODE'S IQ CHANNEL...", 640, 360);
  ask_channel();
}

void leave() {
  if (!g_entered) return;
  if (!g_waiting && !g_failed) visualizer::leave();
  g_waiting = g_failed = g_entered = false;
}

void update(uint32_t now) {
  if (!g_entered) return;
  if (g_waiting) {
    if (g_last_iq_ms || now - g_enter_ms > 4000) open_screen();
    return;
  }
  if (g_failed) return;
  visualizer::set_runtime(runtime());
  rf_analysis::pump();
  visualizer::service_ui(now);
  // The waterfall and audio spectrogram scroll straight inside the panel's buffer 0 (no flip), so that buffer is the screen; every other
  // view hands a finished frame to esp_lcd_panel_draw_bitmap.
  const visualizer::View v = visualizer::view();
  if (v == visualizer::View::waterfall || v == visualizer::View::audio_spectrogram) to_sprite(M5.Display.getPanel()->getFrameBuffer(0));
}

Result touch(int x, int y, bool pressed, uint32_t now) {
  Result r;
  if (!g_entered) return r;
  if (g_waiting) return r;
  if (g_failed) { if (!pressed) r.exit = true; return r; }
  visualizer::handle_touch(x, y, pressed, 1, 0, 0, now);
  visualizer::Action a;
  while (visualizer::take_action(&a)) {
    switch (a.kind) {
      case visualizer::ActionKind::close: r.exit = true; break;
      case visualizer::ActionKind::tune_hz:
        g_center_hz = a.value;
        ask_channel();
        break;
      default: break;   // span (the channel is fixed), sound and volume (no audio path here)
    }
  }
  return r;
}

// {"type":"viz","center":N,"drops":N}
void event(const char* line, uint32_t) {
  const double center = num(line, "center");
  if (center > 0) g_center_hz = (uint32_t)center;
  g_drops = num(line, "drops");
}

void set_num(const char*, double) {}

const char* take_request() {
  if (!g_request_pending) return nullptr;
  g_request_pending = false;
  return g_request;
}

void iq(const uint8_t* data, size_t n) {
  if (!g_entered) return;
  g_last_iq_ms = (uint32_t)emscripten_get_now();
  visualizer::offer_iq(data, n);
}

}  // namespace

extern "C" esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void* data) {
  g_panel_used = true;   // the visualizer flips its own buffers: the frame it just drew is the one on screen
  to_sprite(data);
  return ESP_OK;
}

namespace dscreens {
extern const Screen kViz = {dashboards::Id::count, "viz", enter, leave, update, touch, event, set_num, take_request, nullptr, nullptr, nullptr, iq};
}

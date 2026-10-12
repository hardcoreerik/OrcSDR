#include "sstv_receiver.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace orcsdr::sstv {
namespace {
constexpr uint32_t kRate = 12000;
constexpr uint32_t kQueue = 16384; // power of two, 1.36 seconds of 12k PCM
constexpr uint32_t kQueueMask = kQueue - 1;
constexpr uint16_t kWidth = 320, kMaxRows = 256;
constexpr size_t kImagePixels = static_cast<size_t>(kWidth) * kMaxRows;
constexpr size_t kGallery = 4;
constexpr float kPi = 3.14159265358979323846f;
std::atomic<uint32_t> g_read{0}, g_write{0}, g_overruns{0};
std::atomic<bool> g_enabled{false};
int16_t g_pcm[kQueue]{};
int16_t g_avg = 0;
uint8_t g_decim = 0;
int32_t g_accum = 0;
Mode g_selected = Mode::auto_vis;
Metrics g_metrics{};
Image g_live{};
Image g_archive[kGallery]{};
size_t g_archive_count = 0;
uint32_t g_serial = 0;
uint32_t g_frequency = 145800000;
// Audio PLL estimates tone from rising zero crossings, then smooths periods.
float g_prev = 0, g_tone = 0, g_peak = 0;
uint32_t g_clock = 0, g_last_cross = 0;
float g_ema_period = 0;
uint16_t g_tone_history[300]{};
size_t g_history_pos = 0;
enum class VisPhase : uint8_t { first_leader, break_tone, second_leader, start_bit };
VisPhase g_vis_phase = VisPhase::first_leader;
uint32_t g_phase_samples = 0;
bool g_vis_active = false;
uint32_t g_vis_start = 0;
uint8_t g_vis_bits = 0, g_vis_parity = 0, g_vis_index = 0;
bool g_vis_bad = false;
// Entire line kept as 12k tone estimates; bounded, no heap traffic in audio callback.
uint16_t g_line[6200]{};
size_t g_line_used = 0;
double g_image_samples = 0;
uint16_t g_robot_prev_y[kWidth]{};
uint16_t g_robot_prev_cr[kWidth]{};
bool g_have_prev = false;
uint32_t g_last_frame_serial = 0;

void* alloc_image() {
#ifdef ESP_PLATFORM
  return heap_caps_malloc(kImagePixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::malloc(kImagePixels * sizeof(uint16_t));
#endif
}
void release_image(void* p) {
#ifdef ESP_PLATFORM
  heap_caps_free(p);
#else
  std::free(p);
#endif
}
void clear_decoding() {
  g_vis_active = false;
  g_vis_index = 0;
  g_line_used = 0;
  g_image_samples = 0;
  g_have_prev = false;
  g_metrics.receiving = false;
  g_metrics.row = 0;
}
uint16_t rgb565(int r, int g, int b) {
  r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
  return static_cast<uint16_t>(((r & 248) << 8) | ((g & 252) << 3) | (b >> 3));
}
int intensity(float f) {
  return std::clamp(static_cast<int>(std::lround((f - 1500.f) * (255.f / 800.f))), 0, 255);
}
float average_tone(float begin_ms, float length_ms) {
  const size_t b = std::min(g_line_used, static_cast<size_t>(std::max(0.f, begin_ms) * 12.f));
  const size_t e = std::min(g_line_used, static_cast<size_t>(std::max(0.f, begin_ms + length_ms) * 12.f));
  if (e <= b) return 1900.f;
  uint32_t sum = 0;
  for (size_t i = b; i < e; ++i) sum += g_line[i];
  return static_cast<float>(sum) / static_cast<float>(e - b);
}
uint8_t component(float start_ms, float duration_ms, int pixel) {
  // sample a pixel's middle, averaging over a small temporal window
  const float pixel_ms = duration_ms / 320.f;
  return static_cast<uint8_t>(intensity(average_tone(start_ms + pixel * pixel_ms +
                                               pixel_ms * .25f, pixel_ms * .5f)));
}
double line_ms(Mode m, uint16_t row) {
  switch (m) {
    case Mode::robot36: return 150.0;
    case Mode::martin_m1: return 446.446;
    case Mode::scottie_s1: return row == 0 ? 437.22 : 428.22;
    default: return 150.0;
  }
}
void archive_frame() {
  if (!g_live.pixels) return;
  const size_t tail = kGallery - 1;
  if (g_archive[tail].pixels) release_image(const_cast<uint16_t*>(g_archive[tail].pixels));
  for (size_t i = tail; i > 0; --i) g_archive[i] = g_archive[i - 1];
  auto& dst = g_archive[0];
  dst = g_live;
  ++g_serial;
  dst.serial = g_serial;
  g_live.pixels = static_cast<uint16_t*>(alloc_image());
  g_live.serial = 0;
  if (g_live.pixels) std::memset(const_cast<uint16_t*>(g_live.pixels), 0, kImagePixels * 2);
  g_archive_count = std::min(kGallery, g_archive_count + 1);
  g_metrics.frames++;
  g_last_frame_serial = dst.serial;
  std::snprintf(g_metrics.status, sizeof(g_metrics.status), "IMAGE #%lu COMPLETE",
                static_cast<unsigned long>(g_last_frame_serial));
  g_metrics.receiving = false;
  g_metrics.row = g_metrics.rows;
  g_line_used = 0;
  g_image_samples = 0;
}
void finish_line() {
  if (!g_live.pixels || !g_metrics.receiving) return;
  const uint16_t row = g_metrics.row;
  if (row >= g_metrics.rows) return;
  auto* pixels = const_cast<uint16_t*>(g_live.pixels);
  const size_t base = static_cast<size_t>(row) * kWidth;
  switch (g_metrics.received_mode) {
    case Mode::martin_m1: {
      constexpr float scan = 146.432f;
      constexpr float g_start = 4.862f + .572f;
      constexpr float b_start = g_start + scan + .572f;
      constexpr float r_start = b_start + scan + .572f;
      for (int x = 0; x < kWidth; ++x)
        pixels[base + x] = rgb565(component(r_start, scan, x),
                                  component(g_start, scan, x),
                                  component(b_start, scan, x));
      break;
    }
    case Mode::scottie_s1: {
      constexpr float scan = 138.240f;
      const float gs = (row == 0 ? 9.f : 0.f) + 1.5f;
      const float bs = gs + scan + 1.5f;
      const float rs = bs + scan + 9.f + 1.5f;
      for (int x = 0; x < kWidth; ++x)
        pixels[base + x] = rgb565(component(rs, scan, x),
                                  component(gs, scan, x),
                                  component(bs, scan, x));
      break;
    }
    case Mode::robot36: {
      // Each 150 ms line carries Y and alternates R-Y / B-Y chroma.
      constexpr float ys = 12.f, cs = 106.5f;
      if ((row & 1u) == 0) {
        for (int x = 0; x < kWidth; ++x) {
          const int y = component(ys, 88.f, x);
          g_robot_prev_y[x] = y;
          g_robot_prev_cr[x] = component(cs, 44.f, x);
          pixels[base + x] = rgb565(y, y, y);
        }
        g_have_prev = true;
      } else {
        for (int x = 0; x < kWidth; ++x) {
          const float cr = static_cast<float>(g_robot_prev_cr[x]) - 128.f;
          const float cb = static_cast<float>(component(cs, 44.f, x)) - 128.f;
          const int ya = g_robot_prev_y[x];
          const int yb = component(ys, 88.f, x);
          const auto color = [cr, cb](int y) {
            return rgb565(static_cast<int>(y + 1.402f*cr),
                          static_cast<int>(y - .71414f*cr - .34414f*cb),
                          static_cast<int>(y + 1.772f*cb));
          };
          if (g_have_prev) pixels[base - kWidth + x] = color(ya);
          pixels[base + x] = color(yb);
        }
        g_have_prev = false;
      }
      break;
    }
    default: break;
  }
  g_metrics.row++;
  if (g_metrics.row >= g_metrics.rows) archive_frame();
}
void begin_picture(Mode mode, uint8_t vis) {
  if (!g_live.pixels) g_live.pixels = static_cast<uint16_t*>(alloc_image());
  if (!g_live.pixels) {
    std::strncpy(g_metrics.status, "PSRAM ALLOCATION FAILED", sizeof(g_metrics.status)-1);
    return;
  }
  std::memset(const_cast<uint16_t*>(g_live.pixels), 0, kImagePixels * 2);
  g_live.width = 320;
  g_live.height = mode == Mode::robot36 ? 240 : 256;
  g_live.mode = mode;
  g_live.frequency_hz = g_frequency;
  g_metrics.width = g_live.width;
  g_metrics.rows = g_live.height;
  g_metrics.row = 0;
  g_metrics.received_mode = mode;
  g_metrics.vis = vis;
  g_metrics.receiving = true;
  g_line_used = 0;
  g_image_samples = 0;
  g_have_prev = false;
  std::snprintf(g_metrics.status, sizeof(g_metrics.status), "RECEIVING %s", mode_name(mode));
}
void consume_tone(float f) {
  g_history_pos = (g_history_pos + 1) % 300;
  g_tone_history[g_history_pos] = static_cast<uint16_t>(std::clamp(f, 0.f, 4000.f));
  if (g_metrics.receiving) {
    if (g_line_used < std::size(g_line)) g_line[g_line_used++] = static_cast<uint16_t>(std::clamp(f, 0.f, 3500.f));
    // fractional accounting prevents unbounded drift due to rounded line lengths
    g_image_samples += 1.0;
    const size_t target = static_cast<size_t>(std::lround(line_ms(g_metrics.received_mode, g_metrics.row) * 12.0));
    if (g_line_used >= target) {
      finish_line();
      g_line_used = 0;
      g_image_samples = 0;
    }
    return;
  }
  const bool leader = f >= 1750.f && f <= 2050.f;
  const bool sync = f >= 1050.f && f <= 1370.f;
  if (g_vis_active) {
    const uint32_t elapsed = g_clock - g_vis_start;
    while (g_vis_index < 9 && elapsed >= (540 + g_vis_index*360)) {
      const bool bit_one = f < 1200.f;
      if (g_vis_index < 7) g_vis_bits |= static_cast<uint8_t>(bit_one) << g_vis_index;
      else if (g_vis_index == 7) g_vis_parity = bit_one ? 1 : 0;
      else if (!sync) g_vis_bad = true;
      ++g_vis_index;
    }
    if (elapsed >= 3600) { // 300ms after start => stop field complete
      uint8_t ones = g_vis_parity;
      for (int i = 0; i < 7; ++i) ones += (g_vis_bits >> i) & 1u;
      const bool valid = !g_vis_bad && !(ones & 1u);
      Mode decoded = Mode::auto_vis;
      if (g_vis_bits == 8) decoded = Mode::robot36;
      else if (g_vis_bits == 44) decoded = Mode::martin_m1;
      else if (g_vis_bits == 60) decoded = Mode::scottie_s1;
      g_metrics.good_vis += valid ? 1 : 0;
      g_metrics.bad_vis += valid ? 0 : 1;
      g_vis_active = false;
      g_vis_phase = VisPhase::first_leader;
      g_phase_samples = 0;
      if (valid && decoded != Mode::auto_vis) begin_picture(g_selected == Mode::auto_vis ? decoded : g_selected, g_vis_bits);
      else {
        if (valid) std::snprintf(g_metrics.status, sizeof(g_metrics.status), "UNSUPPORTED VIS %u", g_vis_bits);
        else std::snprintf(g_metrics.status, sizeof(g_metrics.status), "VIS PARITY/SYNC ERROR");
      }
    }
    return;
  }
  // Detect two 1900 Hz leaders separated by a ~10 ms 1200 Hz break.
  // Without two leaders the break would be mistaken for the VIS start.
  switch (g_vis_phase) {
    case VisPhase::first_leader:
      if (leader) {
        if (++g_phase_samples >= 2880) {
          g_vis_phase = VisPhase::break_tone; g_phase_samples = 0;
        }
      } else g_phase_samples = 0;
      break;
    case VisPhase::break_tone:
      if (sync) ++g_phase_samples;
      else if (leader && g_phase_samples >= 72 && g_phase_samples <= 240) {
        g_vis_phase = VisPhase::second_leader; g_phase_samples = 1;
      } else if (!leader || g_phase_samples > 240) {
        g_vis_phase = VisPhase::first_leader; g_phase_samples = 0;
      }
      break;
    case VisPhase::second_leader:
      if (leader) ++g_phase_samples;
      else if (sync && g_phase_samples >= 3000) {
        g_vis_phase = VisPhase::start_bit; g_phase_samples = 1;
      } else {
        g_vis_phase = VisPhase::first_leader; g_phase_samples = 0;
      }
      break;
    case VisPhase::start_bit:
      if (sync) {
        if (++g_phase_samples >= 180) {
          g_vis_active = true;
          g_vis_start = g_clock - g_phase_samples + 1;
          g_vis_bits = g_vis_index = g_vis_parity = 0;
          g_vis_bad = false;
          g_vis_phase = VisPhase::first_leader; g_phase_samples = 0;
          std::snprintf(g_metrics.status, sizeof(g_metrics.status), "VIS HEADER DETECTED");
        }
      } else {
        g_vis_phase = VisPhase::first_leader; g_phase_samples = 0;
      }
      break;
  }
}
void write_u16(std::FILE* f, uint16_t v) { std::fputc(v & 255, f); std::fputc((v >> 8) & 255, f); }
void write_u32(std::FILE* f, uint32_t v) { write_u16(f, v & 65535); write_u16(f, v >> 16); }
} // namespace

const char* mode_name(Mode m) {
  switch (m) {
    case Mode::auto_vis: return "AUTO VIS";
    case Mode::robot36: return "ROBOT 36";
    case Mode::martin_m1: return "MARTIN M1";
    case Mode::scottie_s1: return "SCOTTIE S1";
  }
  return "UNKNOWN";
}
void start(uint32_t hz) {
  g_frequency = hz;
  clear_decoding();
  g_vis_phase = VisPhase::first_leader;
  g_phase_samples = g_clock = 0;
  g_last_cross = 0; g_ema_period = 0; g_tone = 0;
  g_enabled.store(true, std::memory_order_release);
  g_metrics.active = true;
  std::strncpy(g_metrics.status, "WAITING FOR SSTV", sizeof(g_metrics.status)-1);
}
void stop() {
  g_enabled.store(false, std::memory_order_release);
  g_metrics.active = false;
  clear_decoding();
  g_read.store(g_write.load(std::memory_order_acquire), std::memory_order_release);
}
bool active() { return g_enabled.load(std::memory_order_acquire); }
void set_mode(Mode m) { g_selected = m; }
Mode selected_mode() { return g_selected; }
void set_frequency(uint32_t hz) { g_frequency = hz; clear_decoding(); g_phase_samples = 0; g_vis_phase = VisPhase::first_leader; }
void offer_audio_48k(const int16_t* samples, size_t count) {
  if (!active() || !samples) return;
  uint32_t w = g_write.load(std::memory_order_relaxed);
  for (size_t i = 0; i < count; ++i) {
    g_accum += samples[i];
    if (++g_decim != 4) continue;
    g_decim = 0;
    const int16_t value = static_cast<int16_t>(g_accum / 4);
    g_accum = 0;
    const uint32_t r = g_read.load(std::memory_order_acquire);
    if (w - r >= kQueue) { g_overruns.fetch_add(1, std::memory_order_relaxed); continue; }
    g_pcm[w & kQueueMask] = value;
    ++w;
  }
  g_write.store(w, std::memory_order_release);
}
void service(size_t budget) {
  if (!active()) return;
  uint32_t r = g_read.load(std::memory_order_relaxed);
  const uint32_t end = g_write.load(std::memory_order_acquire);
  size_t n = 0;
  while (r != end && n < budget) {
    const float x = static_cast<float>(g_pcm[r & kQueueMask]);
    ++r; ++n; ++g_clock;
    g_peak = std::max(std::abs(x) / 32768.f, g_peak * .9995f);
    // Period-estimation is independent of amplitude; reject implausible crossings.
    if (g_prev <= 0.f && x > 0.f) {
      const uint32_t period = g_clock - g_last_cross;
      if (period >= 4 && period <= 16 && g_last_cross != 0) {
        if (g_ema_period == 0) g_ema_period = static_cast<float>(period);
        else g_ema_period += .18f*(static_cast<float>(period)-g_ema_period);
        g_tone = static_cast<float>(kRate)/g_ema_period;
      }
      g_last_cross = g_clock;
    }
    g_prev = x;
    consume_tone(g_tone);
  }
  g_read.store(r, std::memory_order_release);
  g_metrics.samples += static_cast<uint32_t>(n);
  g_metrics.tone_hz = g_tone;
  g_metrics.audio_peak = g_peak;
  g_metrics.signal = g_peak > .015f;
  g_metrics.audio_overruns = g_overruns.load(std::memory_order_relaxed);
}
Metrics metrics() { return g_metrics; }
Image live_image() { return g_live; }
Image archived_image(size_t n) { return n < g_archive_count ? g_archive[n] : Image{}; }
size_t archive_count() { return g_archive_count; }
void clear_gallery() {
  for (auto& image: g_archive) { if (image.pixels) release_image(const_cast<uint16_t*>(image.pixels)); image = {}; }
  g_archive_count = 0;
}
bool save_bmp(size_t index, char* out, size_t len) {
  Image image = archived_image(index);
  if (!image.pixels || !out || len < 56) return false;
  if (::mkdir("/sd/orcsdr", 0775) != 0) {} // existing directory is fine
  if (::mkdir("/sd/orcsdr/sstv", 0775) != 0) {}
  std::snprintf(out, len, "/sd/orcsdr/sstv/sstv_%lu_%lu.bmp",
      static_cast<unsigned long>(image.serial), static_cast<unsigned long>(image.frequency_hz));
  std::FILE* f = std::fopen(out, "wb");
  if (!f) { out[0] = 0; return false; }
  const uint32_t stride = (image.width*3u+3u)&~3u;
  const uint32_t bytes = stride*image.height;
  std::fputc('B', f); std::fputc('M', f);
  write_u32(f, bytes+54); write_u32(f, 0); write_u32(f, 54);
  write_u32(f, 40); write_u32(f, image.width); write_u32(f, image.height);
  write_u16(f, 1); write_u16(f, 24); write_u32(f, 0); write_u32(f, bytes);
  write_u32(f, 2835); write_u32(f, 2835); write_u32(f, 0); write_u32(f, 0);
  for (int y = image.height - 1; y >= 0; --y) {
    for (int x = 0; x < image.width; ++x) {
      const uint16_t p = image.pixels[static_cast<size_t>(y)*image.width+x];
      const uint8_t b = static_cast<uint8_t>((p&31)<<3);
      const uint8_t g = static_cast<uint8_t>(((p>>5)&63)<<2);
      const uint8_t red = static_cast<uint8_t>(((p>>11)&31)<<3);
      std::fputc(b, f); std::fputc(g, f); std::fputc(red, f);
    }
    for (uint32_t pad=image.width*3; pad < stride; ++pad) std::fputc(0, f);
  }
  const bool written = !std::ferror(f);
  const bool closed = std::fclose(f) == 0;
  const bool ok = written && closed;
  if (!ok) { std::remove(out); out[0] = 0; }
  return ok;
}
bool self_check() {
  return (kQueue & (kQueue-1)) == 0 && kImagePixels == 81920 &&
     line_ms(Mode::robot36, 0) == 150.0 &&
     line_ms(Mode::scottie_s1, 1) > 428.0 &&
     intensity(1500.f) == 0 && intensity(2300.f) == 255;
}
} // namespace orcsdr::sstv

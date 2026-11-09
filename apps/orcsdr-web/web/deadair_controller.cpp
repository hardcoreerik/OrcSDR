// DEAD AIR: OrcSDR's RF anomaly watch (RF level events against an adaptive baseline, demodulated-audio activity, spectrum and
// event review, a generated sweep of tuning steps) over the node's raw IQ channel. The part of main.cpp that supplies it on the
// device (the Hooks: tuned frequency, demodulator, "is the receiver streaming", RF level, retune) is this file.
//
// The page opens a narrow channel on the node (300 kS/s raw IQ) and demodulates it itself, so the screen gets what the device's
// receiver gives it: IQ power for the RF level and spectrum, and demodulated 48 kHz audio for the audio tab. Not available in a
// browser: the journal on an SD card (the screen reports its recorder as failed, as on a device with no card), the Tab5
// microphone, and clip replay. A sweep is the same schedule as on the device: one channel at a time, each hop retunes the node's
// channel, which takes longer than a local retune, and the screen counts that unobserved time as it does anywhere.
#include <M5Unified.h>
#include <emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "dead_air_app.hpp"
#include "dead_air_dashboard.hpp"
#include "dead_air_mic.hpp"
#include "data_screens.hpp"
#include "nvs_store.hpp"
#include "rf_analysis.hpp"
#include "rf_analysis_web.hpp"
#include "time_service.hpp"

namespace orcsdr::time_service { void set_page_clock(uint32_t epoch_seconds); }

// The Tab5 microphone: there is none in this build.
namespace orcsdr::dead_air_mic {
namespace {
bool g_running = false;
}
bool start(Sink) {
  return false;
}
void stop() { g_running = false; }
bool running() { return g_running; }
uint32_t flat_blocks() { return 0; }
Stats stats() {
  Stats s;
  s.error = "NO MICROPHONE IN A BROWSER";
  return s;
}
}  // namespace orcsdr::dead_air_mic

namespace {

using namespace orcsdr;
using namespace dscreens;

constexpr uint32_t kRate = 300000;
constexpr uint32_t kOffset = kRate / 6;   // the node tunes this far below the dial
constexpr const char* kModes[] = {"NFM", "WFM", "AM", "USB", "LSB"};
constexpr uint32_t kBandwidth[] = {12500, 25000, 10000, 2700, 2700};

NvsStore g_nvs;
bool g_installed = false, g_entered = false, g_exit = false;
uint32_t g_hz = 146520000, g_mode = 0, g_signature = 1, g_blocks = 0, g_last_iq_ms = 0;
float g_level = -90.0f;
bool g_level_valid = false;
char g_request[160];
bool g_request_pending = false;
double g_page_ms = 0;
uint32_t g_page_utc = 0;
uint32_t g_last_spectrum_ms = 0;
const char* g_action = nullptr;

void ask_channel() {
  const uint32_t bw = kBandwidth[g_mode];
  snprintf(g_request, sizeof g_request, "{\"op\":\"dachan\",\"hz\":%u,\"mode\":\"%s\",\"bw\":%u}", (unsigned)g_hz,
           g_mode == 1 ? "NFM" : kModes[g_mode], (unsigned)bw);   // the page's demodulator has no wide FM: WFM listens through its FM path
  g_request_pending = true;
}

void retune(uint32_t hz) {
  g_hz = std::max<uint32_t>(24000000u, std::min<uint32_t>(1766000000u, hz));
  ++g_signature;
  g_nvs.begin("orcsdr");
  g_nvs.put_u32("da_hz", g_hz);
  ask_channel();
}

void configure_analysis() {
  rf_analysis::Config c{};
  c.center_hz = g_hz > kOffset ? g_hz - kOffset : g_hz;
  c.span_hz = kRate;
  c.sample_rate_sps = kRate;
  c.measurement_bandwidth_hz = kRate;
  c.fft_size = 1024;
  c.interval_ms = 50;
  rf_analysis::set_config(c);
  rf_analysis::set_enabled(true);
}

dead_air_app::Hooks make_hooks() {
  dead_air_app::Hooks h;
  h.receiver_ready = [] { return g_last_iq_ms && (uint32_t)emscripten_get_now() - g_last_iq_ms < 1500; };
  h.frequency_hz = [] { return g_hz; };
  h.demod = [] { return kModes[g_mode]; };
  h.set_demod = [](uint8_t index) {
    if (index >= sizeof(kModes) / sizeof(kModes[0])) return;
    g_mode = index;
    ++g_signature;
    ask_channel();
  };
  h.release_demod = [] {};
  h.sample_rate = [] { return kRate; };
  h.config_signature = [] { return g_signature; };
  h.rf_sample_counter = [] { return g_blocks; };
  h.rf_level_dbfs = [](float* out) {
    if (!g_level_valid) return false;
    *out = g_level;
    return true;
  };
  h.tune = [](uint32_t hz) { retune(hz); };
  h.hot_retune = [](uint32_t hz) { retune(hz); return true; };
  h.step_frequency = [](uint32_t hz, int direction) {
    const uint32_t step = g_mode == 1 ? 100000u : g_mode == 2 ? 5000u : g_mode >= 3 ? 1000u : 12500u;
    return direction > 0 ? hz + step : hz > step ? hz - step : hz;
  };
  h.sound_enabled = [] { return false; };
  h.overlay_open = [] { return false; };
  h.battery_percent = [] { return -1; };
  h.utc_epoch = [] {
    if (!g_page_utc) return 0u;
    return g_page_utc + (uint32_t)(std::max(0.0, emscripten_get_now() - g_page_ms) / 1000.0);
  };
  h.sd_ready = [] { return false; };
  h.toggle_mute = [] { g_action = "sound_toggle"; };
  h.go_home = [] { g_exit = true; };
  h.open_settings = [] { g_action = "open_device_settings"; };
  return h;
}

void enter(uint32_t) {
  g_nvs.begin("orcsdr");
  g_hz = std::max<uint32_t>(24000000u, g_nvs.get_u32("da_hz", 146520000u));
  g_mode = std::min<uint32_t>(g_nvs.get_u8("da_mode", 0), 4);
  g_exit = false;
  g_level_valid = false;
  g_last_iq_ms = 0;
  g_blocks = 0;
  ++g_signature;
  if (!g_installed) {
    dead_air_app::install(make_hooks());
    g_installed = true;
  }
  rf_analysis::initialize();   // idempotent: the screen measures with the same analysis as RF Lab and the visualizer
  configure_analysis();
  M5.Display.fillScreen(0);
  dead_air_app::open();
  g_entered = true;
  ask_channel();
}

void leave() {
  if (!g_entered) return;
  dead_air_app::close();
  rf_analysis::set_enabled(false);
  g_nvs.begin("orcsdr");
  g_nvs.put_u8("da_mode", (uint8_t)g_mode);
  g_entered = false;
}

void update(uint32_t now) {
  if (!g_entered) return;
  configure_analysis();   // cheap when nothing changed
  rf_analysis::pump();
  if (dead_air_app::wants_spectrum() && now - g_last_spectrum_ms >= 66) {
    rf_analysis::Snapshot* snap = new rf_analysis::Snapshot();
    if (rf_analysis::copy_snapshot(snap) && snap->bins >= 96 && snap->frame_count) {
      // the screen expects raw FFT power in dB over the bins; the analysis gives dBFS, so put the FFT's gain back
      static float levels[rf_analysis::kMaxBins];
      const size_t bins = snap->bins;
      const float gain = 20.0f * std::log10(127.5f * 1.41421356f * (static_cast<float>(bins) - 1.0f) * 0.5f);
      for (size_t i = 0; i < bins; ++i) levels[i] = snap->live[i] + gain;
      dead_air_app::feed_spectrum(levels, bins, kRate);
      g_last_spectrum_ms = now;
    }
    delete snap;
  }
  dead_air_app::tick();
}

Result touch(int x, int y, bool pressed, uint32_t) {
  Result r;
  if (!g_entered || !pressed) return r;
  g_action = nullptr;
  dead_air_app::touch(x, y);
  if (g_exit) r.exit = true;
  r.action = g_action;
  return r;
}

void event(const char*, uint32_t) {}

void set_num(const char* key, double v) {
  if (!strcmp(key, "utc")) {
    g_page_utc = (uint32_t)v;
    g_page_ms = emscripten_get_now();
  }
}

const char* take_request() {
  if (!g_request_pending) return nullptr;
  g_request_pending = false;
  return g_request;
}

// raw 8-bit IQ blocks: the RF level (relative dBFS, a full-scale tone is 0) and the analysis input
void iq(const uint8_t* data, size_t n) {
  if (!g_entered || n < 2) return;
  g_last_iq_ms = (uint32_t)emscripten_get_now();
  double sum = 0;
  for (size_t i = 0; i + 1 < n; i += 2) {
    const double re = (data[i] - 127.5) / 127.5, im = (data[i + 1] - 127.5) / 127.5;
    sum += re * re + im * im;
  }
  const float db = 10.0f * std::log10((float)(sum / (n / 2)) + 1.0e-12f);
  g_level = g_level_valid ? 0.8f * g_level + 0.2f * db : db;
  g_level_valid = true;
  ++g_blocks;
  rf_analysis::offer_iq(data, n);
}

void audio(const float* samples, size_t n) {
  if (!g_entered) return;
  static int16_t block[4096];
  for (size_t off = 0; off < n; off += 4096) {
    const size_t m = std::min<size_t>(4096, n - off);
    for (size_t i = 0; i < m; ++i) block[i] = (int16_t)(std::max(-1.0f, std::min(1.0f, samples[off + i])) * 32000.0f);
    dead_air_app::offer_audio(block, m);
  }
}

}  // namespace

namespace dscreens {
extern const Screen kDeadAir = {dashboards::Id::dead_air, "deadair", enter, leave, update, touch, event, set_num, take_request, nullptr, nullptr, audio, iq};
}

// Diagnostics for the page console (what the device's serial DEAD_AIR line prints).
extern "C" EMSCRIPTEN_KEEPALIVE const char* m5_da_describe() {
  static char text[1200];
  orcsdr::dead_air_app::describe(text, sizeof text);
  return text;
}
extern "C" EMSCRIPTEN_KEEPALIVE const char* m5_da_debug() {
  static char text[200];
  static orcsdr::rf_analysis::Snapshot snap;
  const bool ok = orcsdr::rf_analysis::copy_snapshot(&snap);
  snprintf(text, sizeof text, "copy=%d bins=%u frames=%u wants=%d en=%d", ok, snap.bins, snap.frame_count, orcsdr::dead_air_app::wants_spectrum(), orcsdr::rf_analysis::enabled());
  return text;
}

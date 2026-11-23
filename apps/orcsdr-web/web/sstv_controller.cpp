// SSTV screen: OrcSDR's sstv_app (tuning, detection settings, actions) with the receiver running in this page. The browser demodulates
// the node's IQ channel (USB, LSB or narrow FM, whatever the TUNE tab asks for) and the audio goes straight into OrcSDR's SSTV
// receiver, so the dashboard's progressive picture, sync comb and gallery behave exactly as on the device. Saving to the SD card
// has no meaning here; pictures stay in the RAM gallery (and the node's decode history keeps what its own decoder produced).
#include <M5Unified.h>
#include <emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "dashboard_audio_control.hpp"
#include "data_screens.hpp"
#include "nvs_store.hpp"
#include "sstv_audio_scope.hpp"
#include "sstv_dashboard.hpp"
#include "sstv_receiver.hpp"
#include "sstv_settings.hpp"
#include "sstv_tune.hpp"
#include "time_service.hpp"

namespace orcsdr::time_service { void set_page_clock(uint32_t epoch_seconds); }

namespace {

using namespace orcsdr;
using namespace dscreens;
namespace tn = orcsdr::sstv_tune;
using orcsdr::sstv_settings::Detection;

NvsStore g_nvs;
sstv_settings::Settings g_settings;
uint32_t g_frequency_hz = 145800000u;   // ISS SSTV: the default profile
bool g_entered = false, g_ready = false, g_streaming = false;
uint32_t g_rf_sps = 300000;
float g_rf_dbfs = -90;
char g_request[160];
bool g_request_pending = false;

struct Tune {
  uint8_t step_index = 3;     // 1 kHz
  uint8_t demod_choice = 0;   // 0 = follows the frequency, otherwise 1 + sstv_tune::Demod
  bool rails = true;
  uint8_t if_index = 0;
} g_tune;
constexpr uint32_t kIfWidths[] = {0, 100000, 50000, 25000, 16000, 12000, 8000};

constexpr const char* kKeyHz = "sstv_hz";
constexpr const char* kKeyStep = "sstv_step";
constexpr const char* kKeyDemod = "sstv_demod";
constexpr const char* kKeyRails = "sstv_rails";
constexpr const char* kKeyIf = "sstv_if";

sstv_settings::Store store() {
  g_nvs.begin("orcsdr");
  sstv_settings::Store s;
  s.get_bool = [](const char* k, bool f) { return g_nvs.get_bool(k, f); };
  s.put_bool = [](const char* k, bool v) { g_nvs.put_bool(k, v); };
  s.get_u8 = [](const char* k, uint8_t f) { return g_nvs.get_u8(k, f); };
  s.put_u8 = [](const char* k, uint8_t v) { g_nvs.put_u8(k, v); };
  s.get_u32 = [](const char* k, uint32_t f) { return g_nvs.get_u32(k, f); };
  s.put_u32 = [](const char* k, uint32_t v) { g_nvs.put_u32(k, v); };
  return s;
}

tn::Demod demod_now() { return g_tune.demod_choice ? static_cast<tn::Demod>(g_tune.demod_choice - 1) : tn::default_demod(g_frequency_hz); }

void load_tune() {
  g_tune = Tune{};
  g_frequency_hz = 145800000u;
  g_nvs.begin("orcsdr");
  g_frequency_hz = tn::clamp_hz(g_nvs.get_u32(kKeyHz, g_frequency_hz));
  const uint8_t step = g_nvs.get_u8(kKeyStep, 3);
  g_tune.step_index = step < tn::step_count() ? step : 3;
  const uint8_t demod = g_nvs.get_u8(kKeyDemod, 0);
  g_tune.demod_choice = demod <= 3 ? demod : 0;
  const uint8_t width = g_nvs.get_u8(kKeyIf, 0);
  g_tune.if_index = width < std::size(kIfWidths) ? width : 0;
  g_tune.rails = g_nvs.get_bool(kKeyRails, true);
}

void save_tune() {
  g_nvs.begin("orcsdr");
  g_nvs.put_u32(kKeyHz, g_frequency_hz);
  g_nvs.put_u8(kKeyStep, g_tune.step_index);
  g_nvs.put_u8(kKeyDemod, g_tune.demod_choice);
  g_nvs.put_u8(kKeyIf, g_tune.if_index);
  g_nvs.put_bool(kKeyRails, g_tune.rails);
}

sstv::TuneView tune_view() {
  sstv::TuneView v;
  v.step_index = g_tune.step_index;
  v.demod = static_cast<uint8_t>(demod_now());
  v.demod_auto = g_tune.demod_choice == 0;
  v.rails = g_tune.rails;
  v.if_hz = kIfWidths[g_tune.if_index] ? kIfWidths[g_tune.if_index] : 25000;
  v.if_auto = kIfWidths[g_tune.if_index] == 0;
  v.gain_available = false;   // the browser path has no tuner gain control of its own (the node's gain is on the Home/Setup screens)
  v.rate_sps = g_rf_sps;
  return v;
}

sstv::UiSnapshot snapshot() {
  sstv::UiSnapshot s;
  s.receiver_ready = g_ready;
  s.streaming = g_streaming;
  s.sd_ready = false;
  s.frequency_hz = g_frequency_hz;
  s.rf_dbfs = g_rf_dbfs;
  s.settings = g_settings;
  s.tune = tune_view();
  return s;
}

// The browser opens (or retunes) a channel at this frequency and demodulates it with the chosen mode and filter width.
void request_channel() {
  const tn::Demod d = demod_now();
  const uint32_t bw = kIfWidths[g_tune.if_index];
  snprintf(g_request, sizeof g_request, "{\"op\":\"chan\",\"hz\":%u,\"mode\":\"%s\",\"bw\":%u}", (unsigned)g_frequency_hz,
           d == tn::Demod::usb ? "USB" : d == tn::Demod::lsb ? "LSB" : "NFM", (unsigned)(d == tn::Demod::nfm ? (bw ? bw : 25000) : 2700));
  g_request_pending = true;
}

void retune() {
  sstv::set_frequency(g_frequency_hz);
  request_channel();
  save_tune();
}

bool tune_hz(uint32_t hz) {
  if (hz < tn::kMinHz || hz > tn::kMaxHz) return false;
  if (hz == g_frequency_hz) return true;
  g_frequency_hz = hz;
  retune();
  return true;
}

void commit_settings() {
  sstv_settings::apply(g_settings);
  sstv_settings::save(store(), g_settings);
  sstv::ui_set_settings(g_settings);
}

void audio_observer(const int16_t* samples, size_t count) { sstv_audio_scope::push(samples, count); }

void header_controls() {
  static audio_header::Control control;
  audio_header::draw(control, 40, true, -1);
  audio_header::draw_home_button();
  audio_header::draw_settings_button();
  audio_header::draw_mute_button(true);
  audio_header::draw_visualizer_button(true);
}

uint32_t utc_source() { return time_service::now().utc; }

void enter(uint32_t) {
  g_settings = sstv_settings::load(store());
  load_tune();
  sstv_settings::apply(g_settings);
  sstv::set_utc_source(utc_source);
  sstv::set_header_hook(header_controls);
  sstv::start(g_frequency_hz);
  (void)sstv_audio_scope::start();
  sstv::set_audio_observer(audio_observer);
  sstv::ui_enter(snapshot());
  g_entered = true;
  request_channel();
}

void leave() {
  if (!g_entered) return;
  sstv::set_audio_observer(nullptr);
  sstv_audio_scope::stop();
  sstv::stop();
  sstv::ui_leave();
  g_entered = false;
}

void update(uint32_t) {
  if (!g_entered) return;
  sstv::service();
  sstv::ui_update(snapshot());
}

void handle(const sstv::Action& action) {
  using Kind = sstv::ActionKind;
  switch (action.kind) {
    case Kind::tune_profile:
      if ((size_t)action.value < std::size(sstv::kProfiles)) tune_hz(sstv::kProfiles[action.value].hz);
      break;
    case Kind::tune_hz:
      if (!tune_hz((uint32_t)action.value)) sstv::ui_notice("FREQUENCY OUT OF RANGE");
      break;
    case Kind::nudge:
      tune_hz(tn::nudge(g_frequency_hz, tn::step_hz(g_tune.step_index), action.value < 0 ? -1 : 1));
      break;
    case Kind::set_step:
      if (action.value >= 0 && (size_t)action.value < tn::step_count()) { g_tune.step_index = (uint8_t)action.value; save_tune(); }
      break;
    case Kind::cycle_demod:
      g_tune.demod_choice = (uint8_t)((g_tune.demod_choice + 1) % 4);
      request_channel();
      save_tune();
      break;
    case Kind::cycle_if:
      g_tune.if_index = (uint8_t)((g_tune.if_index + 1) % std::size(kIfWidths));
      request_channel();
      save_tune();
      break;
    case Kind::toggle_rails: g_tune.rails = !g_tune.rails; save_tune(); break;
    case Kind::apply_correction:
      tune_hz((uint32_t)std::clamp<int64_t>((int64_t)g_frequency_hz + action.value, tn::kMinHz, tn::kMaxHz));
      break;
    case Kind::set_detection:
      g_settings = sstv_settings::with_detection(g_settings, (Detection)action.value);
      commit_settings();
      break;
    case Kind::select_mode:
      g_settings = sstv_settings::with_manual_mode(g_settings, (sstv::Mode)action.value);
      commit_settings();
      break;
    case Kind::toggle_autosave:
      sstv::ui_notice("NO SD CARD IN A BROWSER - GALLERY IS IN MEMORY");
      break;
    case Kind::save_image:
      sstv::ui_notice("PICTURES STAY IN THE RAM GALLERY");
      break;
    case Kind::clear_gallery: sstv::clear_gallery(); sstv::ui_notice("RAM GALLERY CLEARED"); break;
    case Kind::abort_rx: sstv::abort_image(); sstv::ui_notice("IMAGE ABORTED / SEARCHING"); break;
    default: break;   // gain_*: the node's tuner gain is not exposed per screen here
  }
}

Result touch(int x, int y, bool pressed, uint32_t) {
  Result r;
  if (!pressed) return r;
  if (audio_header::home_hit(x, y)) { r.exit = true; return r; }
  if (audio_header::settings_hit(x, y) || audio_header::mute_hit(x, y) || audio_header::visualizer_hit(x, y)) return r;
  handle(sstv::ui_touch(x, y));
  return r;
}

void event(const char*, uint32_t) {}

void set_num(const char* key, double v) {
  if (!strcmp(key, "receiving")) { g_streaming = v != 0; g_ready = true; }
  else if (!strcmp(key, "rf_dbfs")) g_rf_dbfs = (float)v;
  else if (!strcmp(key, "sps")) g_rf_sps = (uint32_t)v;
  else if (!strcmp(key, "utc")) time_service::set_page_clock((uint32_t)v);
}

void audio(const float* samples, size_t n) {
  if (!g_entered) return;
  static int16_t block[4096];
  for (size_t off = 0; off < n; off += 4096) {
    const size_t m = std::min<size_t>(4096, n - off);
    for (size_t i = 0; i < m; ++i) {
      const float v = std::max(-1.0f, std::min(1.0f, samples[off + i]));
      block[i] = (int16_t)(v * 30000.0f);
    }
    sstv::offer_audio_48k(block, m);
  }
}

void spectrum(const float* levels, int n, float) {
  if (g_entered && sstv::ui_wants_spectrum()) sstv::ui_feed_spectrum(levels, (size_t)n, g_rf_sps);
}

const char* take_request() {
  if (!g_request_pending) return nullptr;
  g_request_pending = false;
  return g_request;
}

}  // namespace

namespace dscreens {
extern const Screen kSstv = {dashboards::Id::sstv, "sstv", enter, leave, update, touch, event, set_num, take_request, spectrum, nullptr, audio};
}

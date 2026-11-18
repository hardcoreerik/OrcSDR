// DEAD AIR on the P4 console: the shared hook-driven app (dead_air_app, apps/orcsdr-tab5/ui, unchanged) on the compact dashboard
// (dead_air_compact_dashboard.cpp): OVERVIEW, SPECTRUM, EVENTS, SETUP. Receiver only: no environment sensor, no microphone, no replay,
// and no Audio, ENV, Review, scrub, control-run, session or VEIL-26 views on this target yet.
#include <algorithm>
#include "audio_service.hpp"
#include "esp_timer.h"
#include "dead_air_app.hpp"
#include "dead_air_compact_dashboard.hpp"
#include "home_app.hpp"
#include "orcsdr_storage.hpp"
#include "present.hpp"
#include "radio_service.hpp"
#include "screens.hpp"
#include "ui_support.hpp"

namespace {
namespace da = orcsdr::dead_air;
uint8_t g_mode = 0;   // NFM, WFM, AM, USB, LSB as the app's mode list

bool resize_viewport() {
  const auto* g = present_geometry();
  return da::ui_set_viewport(g ? da::compact::Rect{int(g->safe.x), int(g->safe.y), int(g->safe.width), int(g->safe.height)}
                               : da::compact::Rect{0, 0, 1280, 720});
}

uint32_t tuned_hz() {
  orcsdr::radio::ReceiverInfo info;
  return orcsdr::radio::receiver_info(0, &info) ? info.frequency_hz : 0;
}

bool streaming() { return orcsdr::radio::snapshot().receiver_state == 2; }

void pcm_tap(const int16_t* pcm, size_t count) { orcsdr::dead_air_app::offer_audio(pcm, count); }

orcsdr::dead_air_app::Hooks app_hooks() {
  orcsdr::dead_air_app::Hooks h;
  h.receiver_ready = streaming;
  h.frequency_hz = tuned_hz;
  h.demod = [] {
    static const char* const kNames[] = {"NFM", "WFM", "AM", "USB", "LSB"};
    return kNames[std::min<size_t>(g_mode, 4)];
  };
  h.set_demod = [](uint8_t index) {
    static const orcsdr::dsp::Mode kModes[] = {orcsdr::dsp::Mode::nfm, orcsdr::dsp::Mode::wfm, orcsdr::dsp::Mode::am, orcsdr::dsp::Mode::usb, orcsdr::dsp::Mode::lsb};
    if (index >= 5) return;
    g_mode = index;
    audio_service::set_mode(kModes[index], true);
  };
  h.release_demod = [] {};
  h.sample_rate = [] { return orcsdr::radio::sample_rate(); };
  h.config_signature = [] {
    uint32_t v = tuned_hz() * 2654435761u;
    v ^= orcsdr::radio::sample_rate() + 0x9e3779b9u + (v << 6) + (v >> 2);
    v ^= (uint32_t(g_mode) + 1u) * 668265263u;
    const auto g = orcsdr::radio::gain();
    v ^= uint32_t(g.tenth_db) * 2246822519u + (g.auto_mode ? 3266489917u : 0u);
    return v;
  };
  h.rf_sample_counter = [] { return uint32_t(esp_timer_get_time() / 100000); };   // the level is refreshed with every IQ block
  h.rf_level_dbfs = [](float* out) {
    const float v = orcsdr::radio::level_dbfs();
    if (v <= -89.9f) return false;
    *out = v;
    return true;
  };
  h.tune = [](uint32_t hz) { home_app::request_frequency(hz); };
  h.hot_retune = [](uint32_t hz) { home_app::request_frequency(hz); return true; };
  h.step_frequency = [](uint32_t hz, int direction) {
    const int64_t next = int64_t(hz) + int64_t(direction) * 25000;
    return uint32_t(std::max<int64_t>(next, 1000));
  };
  h.sound_enabled = [] { return ui_support::sound_enabled(); };
  h.overlay_open = [] { return ui_support::header_overlay_open(); };
  h.battery_percent = [] { return -1; };
  h.utc_epoch = ui_support::utc_epoch;
  h.sd_ready = [] { return orcsdr::storage::mounted(); };
  h.toggle_mute = [] {};
  h.go_home = [] { home_app::request_home(); };
  h.open_settings = [] { home_app::request_open(orcsdr::dashboards::Id::settings); };
  return h;
}

void enter() {
  da::ui_set_header_controls(ui_support::draw_header);
  if (!resize_viewport()) {
    home_app::request_home();
    return;
  }
  orcsdr::dead_air_app::install(app_hooks());
  if (!orcsdr::dead_air_app::ready_to_open()) {
    home_app::request_home();
    return;
  }
  ui_support::header_reset();
  ui_support::claim_screen(orcsdr::screens::Id::dead_air);
  g_mode = 0;
  audio_service::set_active(true);
  audio_service::set_mode(orcsdr::dsp::Mode::nfm, true);
  audio_service::set_pcm_tap(pcm_tap);
  orcsdr::dead_air_app::open();
}

void leave() {
  audio_service::set_pcm_tap(nullptr);
  orcsdr::dead_air_app::close();
}

void tick() {
  orcsdr::dead_air_app::tick();
  ui_support::header_service();
}

void tap(int x, int y) {
  if (ui_support::handle_header_touch(x, y, true)) return;
  orcsdr::dead_air_app::touch(x, y);
}

void spectrum(const float* levels, size_t, size_t, float) {
  if (orcsdr::dead_air_app::wants_spectrum()) orcsdr::dead_air_app::feed_spectrum(levels, 256, orcsdr::radio::sample_rate());
}
}  // namespace

namespace screens {
const Screen& dead_air_screen() {
  static const Screen s{orcsdr::dashboards::Id::dead_air, "DEAD_AIR", enter, leave, tick, tap, nullptr, spectrum, nullptr, []() {
    if (!resize_viewport()) return false;
    orcsdr::dead_air_app::draw(true);
    return true;
  }};
  return s;
}
}  // namespace screens

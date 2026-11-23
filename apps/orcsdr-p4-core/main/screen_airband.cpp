// AIRBAND dashboard (apps/orcsdr-tab5/ui/airband_dashboard.cpp, airband_runtime.cpp, airband_scanner.cpp and
// airband_catalog.cpp, unchanged) on the P4 console. The runtime module owns the scanner, squelch and screens;
// this adapter supplies its live state and the hooks it calls.
#include <algorithm>
#include "airband_runtime.hpp"
#include "audio_service.hpp"
#include "esp_timer.h"
#include "home_app.hpp"
#include "radio_service.hpp"
#include "screens.hpp"

namespace {
namespace ab = orcsdr::airband;
uint32_t g_filter_hz = 10000;
bool g_configured = false;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

bool hook_tune(uint32_t hz) {
  home_app::request_frequency(hz);
  return true;
}
void hook_home() { home_app::request_home(); }
void hook_settings() { home_app::request_open(orcsdr::dashboards::Id::settings); }
bool hook_gain(const orcsdr::receiver_controls::Action& a) {
  using K = orcsdr::receiver_controls::ActionKind;
  switch (a.kind) {
    case K::gain_tenth_db: orcsdr::radio::set_gain_manual(a.value); return true;
    case K::tuner_agc: a.value ? orcsdr::radio::set_gain_auto() : orcsdr::radio::set_gain_manual(orcsdr::radio::gain().tenth_db); return true;
    case K::rtl_agc: orcsdr::radio::set_rtl_agc(a.value != 0); return true;
    default: return false;
  }
}
void hook_filter(uint32_t hz) { g_filter_hz = hz; }
void hook_span_step(int dir) {
  const uint32_t span = home_app::status().span_hz;
  home_app::request_span(dir < 0 ? span / 2 : span * 2);
}
void hook_span_set(uint32_t hz) {
  if (hz) home_app::request_span(hz);
}

const ab::LiveState& live() {
  static ab::LiveState s;
  const home_app::Status st = home_app::status();
  const auto rs = orcsdr::radio::snapshot();
  const orcsdr::radio::Gain g = orcsdr::radio::gain();
  s.now_ms = now_ms();
  s.frequency_hz = st.frequency_hz;
  s.channel_db = audio_service::channel_dbfs();
  s.receiver_running = rs.receiver_state == 2;
  s.sound_enabled = st.sound;
  s.battery_percent = -1;
  s.location_configured = false;
  s.controls.route = orcsdr::shortwave::ReceiverRoute::tuner;
  s.controls.capabilities.rf_gain = g.available;
  s.controls.capabilities.tuner_agc = g.auto_available;
  s.controls.capabilities.rtl_agc = g.rtl_agc_available;
  s.controls.tuner_agc = g.auto_mode;
  s.controls.rtl_agc = g.rtl_agc;
  s.controls.volume = st.volume;
  s.controls.gain_tenth_db = static_cast<int16_t>(g.tenth_db);
  s.gain_step_count = g.step_count;
  for (int i = 0; i < g.step_count; ++i) s.gain_steps_tenth_db[i] = static_cast<int16_t>(g.steps[i]);
  s.filesystem = nullptr;  // no card on this target yet: the airport catalog stays empty
  return s;
}

void enter() {
  if (!g_configured) {
    ab::Hooks h;
    h.tune = hook_tune;
    h.show_home = hook_home;
    h.open_radio_settings = hook_settings;
    h.open_location_settings = hook_settings;
    h.apply_gain = hook_gain;
    h.apply_filter = hook_filter;
    h.scope_span_step = hook_span_step;
    h.scope_span_set = hook_span_set;
    ab::configure(h);
    g_configured = true;
  }
  const home_app::Status st = home_app::status();
  if (!ab::in_band(st.frequency_hz)) home_app::request_frequency(ab::default_frequency());
  audio_service::set_mode(orcsdr::dsp::Mode::am, true);
  ab::enter(live());
}
void leave() {
  audio_service::set_gate(true);
  ab::leave();
}
void tick() {
  audio_service::set_mode(orcsdr::dsp::Mode::am, true);
  const ab::LiveState& l = live();
  ab::update(l);
  ab::service(l);
  audio_service::set_gate(ab::audio_open());
}
bool g_dragging = false;  // a finger is dragging one of the scope's filter lines
void tap(int x, int y) {
  if (g_dragging) {
    g_dragging = false;  // the lift that ends a drag is not a tap
    return;
  }
  ab::handle_touch(x, y, live());
}
void drag(int x, int y) {
  if (g_dragging || ab::scope_edge_hit(x, y)) {
    g_dragging = true;
    ab::scope_drag_filter(x, live());
  }
}
void spectrum(const float* levels, size_t first, size_t visible, float floor) {
  (void)floor;
  ab::draw_spectrum(levels, first, visible, home_app::status().span_hz);
}
}  // namespace

namespace screens {
const Screen& airband_screen() {
  static const Screen s{orcsdr::dashboards::Id::airband, "AIRBAND", enter, leave, tick, tap, drag, spectrum};
  return s;
}
}  // namespace screens

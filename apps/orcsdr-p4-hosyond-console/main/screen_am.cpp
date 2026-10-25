// AM RADIO dashboard (apps/orcsdr-tab5/ui/am_dashboard.cpp, unchanged) on the P4 console.
#include <algorithm>
#include "am_dashboard.hpp"
#include "audio_service.hpp"
#include "home_app.hpp"
#include "nvs_store.hpp"
#include "radio_service.hpp"
#include "screens.hpp"

namespace {
namespace am = orcsdr::am;
uint32_t g_step_hz = 10000;
uint32_t g_filter_hz = 10000;
bool g_graphics = true;
bool g_loaded = false;

am::Snapshot build() {
  am::Snapshot s{};
  const home_app::Status st = home_app::status();
  const auto rs = orcsdr::radio::snapshot();
  s.frequency_hz = st.frequency_hz;
  s.step_hz = g_step_hz;
  s.filter_bandwidth_hz = g_filter_hz;
  s.span_hz = st.span_hz;
  s.relative_dbfs = orcsdr::radio::level_dbfs();
  s.running = rs.receiver_state == 2;
  s.driver_ready = rs.receiver_state == 2;
  s.sound_enabled = st.sound;
  s.graphics_enabled = g_graphics;
  s.volume = st.volume;
  s.battery_percent = -1;
  const orcsdr::radio::Gain g = orcsdr::radio::gain();
  s.gain_available = g.available && s.driver_ready;
  s.rtl_agc_available = g.rtl_agc_available && st.frequency_hz < 24000000;
  s.rtl_agc = g.rtl_agc;
  s.gain_auto = g.auto_mode;
  s.gain_tenth_db = g.tenth_db;
  s.gain_step_count = g.step_count;
  for (int i = 0; i < g.step_count; ++i) s.gain_steps_tenth_db[i] = g.steps[i];
  am::populate_presets(s);
  return s;
}

void enter() {
  if (!g_loaded) {
    orcsdr::NvsStore store;
    if (store.begin("orcsdr_am", true)) {
      am::load(store);
      store.end();
    }
    g_loaded = true;
    g_step_hz = am::tune_step();
  }
  // AM broadcast: start from the saved station if Home is not already in the AM band.
  const home_app::Status st = home_app::status();
  if (st.frequency_hz > 30000000 && am::saved_frequency()) home_app::request_frequency(am::saved_frequency());
  am::enter(build());
}
void leave() { am::leave(); }
void tick() {
  audio_service::set_mode(orcsdr::dsp::Mode::am, true);
  am::update(build());
}

void apply(const am::Action& a) {
  const home_app::Status st = home_app::status();
  auto tune = [](uint32_t hz) {
    am::note_tuned(hz);
    home_app::request_frequency(hz);
  };
  switch (a.kind) {
    case am::ActionKind::tune_hz: tune(a.value); break;
    case am::ActionKind::step_down: tune(st.frequency_hz > g_step_hz ? st.frequency_hz - g_step_hz : st.frequency_hz); break;
    case am::ActionKind::step_up: tune(st.frequency_hz + g_step_hz); break;
    case am::ActionKind::step_cycle: g_step_hz = am::cycle_tune_step(); break;
    case am::ActionKind::scan_spacing_toggle: (void)am::toggle_scan_spacing(); break;
    case am::ActionKind::filter_bandwidth_hz: g_filter_hz = std::clamp<uint32_t>(a.value, 2000, 20000); break;
    case am::ActionKind::span_down: home_app::request_span(st.span_hz / 2); break;
    case am::ActionKind::span_up: home_app::request_span(st.span_hz * 2); break;
    case am::ActionKind::preset_recall:
      if (const uint32_t hz = am::preset(a.value)) tune(hz);
      break;
    case am::ActionKind::preset_save: am::save_current_preset(); break;
    case am::ActionKind::preset_replace: (void)am::replace_preset(a.value); break;
    case am::ActionKind::preset_delete: (void)am::delete_preset(a.value); break;
    case am::ActionKind::sound_toggle: home_app::request_sound(!st.sound); break;
    case am::ActionKind::volume_down: home_app::request_volume(static_cast<uint8_t>(std::max(0, int(st.volume) - 16))); break;
    case am::ActionKind::volume_up: home_app::request_volume(static_cast<uint8_t>(std::min(255, int(st.volume) + 16))); break;
    case am::ActionKind::graphics_toggle: g_graphics = !g_graphics; break;
    case am::ActionKind::gain_auto: orcsdr::radio::set_gain_auto(); break;
    case am::ActionKind::gain_tenth_db: orcsdr::radio::set_gain_manual(static_cast<int>(a.value)); break;
    case am::ActionKind::rtl_agc: orcsdr::radio::set_rtl_agc(a.value != 0); break;
    case am::ActionKind::open_device_settings: home_app::request_open(orcsdr::dashboards::Id::settings); break;
    case am::ActionKind::exit_home: home_app::request_home(); break;
    default: break;  // recording, band scan: not available on this target yet
  }
}

void tap(int x, int y) {
  const am::TouchResult preset = am::handle_preset_touch(x, y, false, static_cast<uint32_t>(esp_timer_get_time() / 1000));
  if (preset.consumed) {
    apply(preset.action);
    return;
  }
  apply(am::handle_touch(x, y));
}
void drag(int x, int y) {
  am::Action a = am::handle_bandwidth_drag(x, y);
  if (a.kind == am::ActionKind::none) a = am::handle_gain_drag(x, y);
  apply(a);
}
void spectrum(const float* levels, size_t first, size_t visible, float floor) { am::draw_spectrum(levels, first, visible, floor); }
}  // namespace

namespace screens {
const Screen& am_screen() {
  static const Screen s{orcsdr::dashboards::Id::am, "AM", enter, leave, tick, tap, drag, spectrum};
  return s;
}
}  // namespace screens

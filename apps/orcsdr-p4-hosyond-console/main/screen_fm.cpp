// FM RADIO dashboard (apps/orcsdr-tab5/ui/fm_dashboard.cpp, unchanged) on the P4 console.
// This adapter does what the Tab5's fm_dashboard_snapshot() / handle_fm_dashboard_action() do there.
#include <algorithm>
#include <cstring>
#include "audio_service.hpp"
#include "fm_dashboard.hpp"
#include "home_app.hpp"
#include "radio_service.hpp"
#include "screens.hpp"

namespace {
namespace fm = orcsdr::fm;
uint32_t g_step_hz = 100000;
uint32_t g_filter_hz = 180000;
bool g_graphics = true;

fm::Snapshot build() {
  fm::Snapshot s;
  const home_app::Status st = home_app::status();
  const auto rs = orcsdr::radio::snapshot();
  s.frequency_hz = st.frequency_hz;
  s.step_hz = g_step_hz;
  s.filter_bandwidth_hz = g_filter_hz;
  s.span_hz = st.span_hz;
  s.relative_dbfs = orcsdr::radio::level_dbfs();
  s.running = rs.receiver_state == 2;
  s.driver_ready = rs.receiver_state == 2;
  s.effective_sps = rs.effective_sps;
  s.target_sps = orcsdr::radio::sample_rate();
  s.sound_enabled = st.sound;
  s.graphics_enabled = g_graphics;
  s.volume = st.volume;
  s.battery_percent = -1;
  const audio_service::Status a = audio_service::status();
  s.audio_underruns = a.underruns;
  s.consumer_drops = a.dropped_blocks;
  return s;
}

void enter() { fm::enter(build()); }
void leave() { fm::leave(); }
void tick() { fm::update(build()); }

void tap(int x, int y) {
  const fm::Action a = fm::handle_touch(x, y);
  const home_app::Status st = home_app::status();
  switch (a.kind) {
    case fm::ActionKind::tune_hz: home_app::request_frequency(a.value); break;
    case fm::ActionKind::step_down:
    case fm::ActionKind::seek_down:
      home_app::request_frequency(st.frequency_hz > g_step_hz ? st.frequency_hz - g_step_hz : st.frequency_hz);
      break;
    case fm::ActionKind::step_up:
    case fm::ActionKind::seek_up: home_app::request_frequency(st.frequency_hz + g_step_hz); break;
    case fm::ActionKind::step_cycle:
    case fm::ActionKind::step_cycle_previous: {
      static constexpr uint32_t steps[] = {50000, 100000, 200000, 500000, 1000000};
      size_t i = 0;
      while (i < std::size(steps) && steps[i] != g_step_hz) ++i;
      const size_t n = std::size(steps);
      g_step_hz = steps[(i + (a.kind == fm::ActionKind::step_cycle_previous ? n - 1 : 1)) % n];
      break;
    }
    case fm::ActionKind::filter_down: g_filter_hz = std::max<uint32_t>(g_filter_hz > 20000 ? g_filter_hz - 20000 : 0, 60000); break;
    case fm::ActionKind::filter_up: g_filter_hz = std::min<uint32_t>(g_filter_hz + 20000, 300000); break;
    case fm::ActionKind::span_down: home_app::request_span(st.span_hz / 2); break;
    case fm::ActionKind::span_up: home_app::request_span(st.span_hz * 2); break;
    case fm::ActionKind::sound_toggle: home_app::request_sound(!st.sound); break;
    case fm::ActionKind::volume_down: home_app::request_volume(static_cast<uint8_t>(std::max(0, int(st.volume) - 16))); break;
    case fm::ActionKind::volume_up: home_app::request_volume(static_cast<uint8_t>(std::min(255, int(st.volume) + 16))); break;
    case fm::ActionKind::graphics_toggle: g_graphics = !g_graphics; break;
    case fm::ActionKind::exit_to_browse: home_app::request_home(); break;
    case fm::ActionKind::open_device_settings: home_app::request_open(orcsdr::dashboards::Id::settings); break;
    default: break;  // recording, presets scan, receiver gain: not available on this target yet
  }
}

void drag(int x, int y) { (void)fm::handle_gain_drag(x, y); }
void spectrum(const float* levels, size_t first, size_t visible, float floor) { fm::draw_spectrum(levels, first, visible, floor); }
}  // namespace

namespace screens {
const Screen& fm_screen() {
  static const Screen s{orcsdr::dashboards::Id::fm, "FM", enter, leave, tick, tap, drag, spectrum};
  return s;
}
}  // namespace screens

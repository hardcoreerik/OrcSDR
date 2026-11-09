// SHORTWAVE dashboard (apps/orcsdr-tab5/ui/shortwave_dashboard.cpp and its model, unchanged) on the P4 console.
// AM, USB and LSB are demodulated; the audio clean-up chain, band hunt and the SD library (memories and logbook)
// are not available on this target yet.
#include <algorithm>
#include <cstring>
#include <new>
#include "esp_heap_caps.h"
#include "audio_service.hpp"
#include "home_app.hpp"
#include "radio_service.hpp"
#include "screens.hpp"
#include "shortwave_dashboard.hpp"

namespace {
namespace sw = orcsdr::shortwave;
uint32_t g_step_hz = 1000;
uint32_t g_filter_hz = 6000;
int g_mode = 0;  // 0 follow the band (AM), 1 AM, 2 USB, 3 LSB
int16_t g_clarifier = 0;

// Large tables live in PSRAM: as statics they took 136 KB of the internal RAM the graphics library needs.
template <typename T>
T& psram_table() {
  static T* t = new (heap_caps_calloc(1, sizeof(T), MALLOC_CAP_SPIRAM)) T();
  return *t;
}
sw::MemoryTable& memories() { return psram_table<sw::MemoryTable>(); }
sw::LogTable& logs() { return psram_table<sw::LogTable>(); }

const char* mode_text() { return g_mode == 2 ? "USB" : g_mode == 3 ? "LSB" : "AM"; }

void apply_mode() {
  audio_service::set_mode(g_mode == 2 ? orcsdr::dsp::Mode::usb : g_mode == 3 ? orcsdr::dsp::Mode::lsb : orcsdr::dsp::Mode::am, true);
}

sw::Snapshot& build() {
  static sw::Snapshot s;
  const home_app::Status st = home_app::status();
  const auto rs = orcsdr::radio::snapshot();
  const orcsdr::radio::Gain g = orcsdr::radio::gain();
  s.frequency_hz = st.frequency_hz;
  s.step_hz = g_step_hz;
  s.filter_bandwidth_hz = g_mode >= 2 ? 2400 : g_filter_hz;
  s.span_hz = st.span_hz;
  s.relative_dbfs = orcsdr::radio::level_dbfs();
  s.running = rs.receiver_state == 2;
  s.driver_ready = rs.receiver_state == 2;
  s.sound_enabled = st.sound;
  s.battery_percent = -1;
  std::snprintf(s.device, sizeof(s.device), "RTL-SDR");
  s.controls.route = sw::ReceiverRoute::tuner;
  s.controls.capabilities.rf_gain = g.available;
  s.controls.capabilities.tuner_agc = g.auto_available;
  s.controls.capabilities.rtl_agc = g.rtl_agc_available;
  s.controls.tuner_agc = g.auto_mode;
  s.controls.rtl_agc = g.rtl_agc;
  s.controls.volume = st.volume;
  s.controls.gain_tenth_db = static_cast<int16_t>(g.tenth_db);
  s.gain_step_count = g.step_count;
  for (int i = 0; i < g.step_count; ++i) s.gain_steps_tenth_db[i] = g.steps[i];
  s.memories = &memories();
  s.logs = &logs();
  s.memory_count = 0;
  s.log_count = 0;
  s.storage_status = orcsdr::shortwave::StorageStatus::unavailable;
  s.utc_valid = false;
  std::snprintf(s.mode, sizeof(s.mode), "%s", mode_text());
  s.mode_auto = g_mode == 0;
  s.clarifier_hz = g_clarifier;
  return s;
}

void enter() {
  const home_app::Status st = home_app::status();
  if (st.frequency_hz < 100000 || st.frequency_hz > 30000000) home_app::request_frequency(sw::saved_frequency());
  apply_mode();
  sw::enter(build());
}
void leave() { sw::leave(); }
void tick() {
  apply_mode();
  sw::update(build());
}

void act(const sw::Action& a) {
  const home_app::Status st = home_app::status();
  auto tune = [](uint32_t hz) {
    sw::note_tuned(hz);
    home_app::request_frequency(hz);
  };
  switch (a.kind) {
    case sw::ActionKind::tune_hz: tune(static_cast<uint32_t>(a.value)); break;
    case sw::ActionKind::open_frequency: sw::begin_frequency_entry(); break;
    case sw::ActionKind::step_down: tune(st.frequency_hz > g_step_hz ? st.frequency_hz - g_step_hz : st.frequency_hz); break;
    case sw::ActionKind::step_up: tune(st.frequency_hz + g_step_hz); break;
    case sw::ActionKind::step_cycle: {
      static constexpr uint32_t steps[] = {100, 500, 1000, 5000, 9000, 10000};
      size_t i = 0;
      while (i < std::size(steps) && steps[i] != g_step_hz) ++i;
      g_step_hz = steps[(i + 1) % std::size(steps)];
      break;
    }
    case sw::ActionKind::filter_down: g_filter_hz = std::max<uint32_t>(2000, g_filter_hz - 1000); break;
    case sw::ActionKind::filter_up: g_filter_hz = std::min<uint32_t>(15000, g_filter_hz + 1000); break;
    case sw::ActionKind::filter_cycle: g_filter_hz = g_filter_hz <= 3000 ? 6000 : g_filter_hz <= 6000 ? 9000 : 3000; break;
    case sw::ActionKind::filter_bandwidth_hz: g_filter_hz = std::clamp<uint32_t>(static_cast<uint32_t>(a.value), 1000, 15000); break;
    case sw::ActionKind::span_down: home_app::request_span(st.span_hz / 2); break;
    case sw::ActionKind::span_up: home_app::request_span(st.span_hz * 2); break;
    case sw::ActionKind::span_hz: home_app::request_span(static_cast<uint32_t>(a.value)); break;
    case sw::ActionKind::sound_toggle: home_app::request_sound(!st.sound); break;
    case sw::ActionKind::volume_down: home_app::request_volume(static_cast<uint8_t>(std::max(0, int(st.volume) - 16))); break;
    case sw::ActionKind::volume_up: home_app::request_volume(static_cast<uint8_t>(std::min(255, int(st.volume) + 16))); break;
    case sw::ActionKind::gain_auto: orcsdr::radio::set_gain_auto(); break;
    case sw::ActionKind::gain_tenth_db: orcsdr::radio::set_gain_manual(a.value); break;
    case sw::ActionKind::rtl_agc: orcsdr::radio::set_rtl_agc(a.value != 0); break;
    case sw::ActionKind::mode_cycle: g_mode = (g_mode + 1) % 4; break;
    case sw::ActionKind::clarifier_down: g_clarifier = static_cast<int16_t>(std::max(-1000, int(g_clarifier) - 50)); break;
    case sw::ActionKind::clarifier_up: g_clarifier = static_cast<int16_t>(std::min(1000, int(g_clarifier) + 50)); break;
    case sw::ActionKind::open_settings: home_app::request_open(orcsdr::dashboards::Id::settings); break;
    case sw::ActionKind::exit_home: home_app::request_home(); break;
    default: break;  // memories, logbook, hunt, audio clean-up: not available on this target yet
  }
}

void tap(int x, int y) { act(sw::handle_touch(x, y)); }
void drag(int x, int y) {
  act(sw::handle_gain_drag(x, y));
  act(sw::handle_filter_drag(x, y, true));
}
void spectrum(const float* levels, size_t first, size_t visible, float floor) { sw::draw_spectrum(levels, first, visible, floor); }
}  // namespace

namespace screens {
const Screen& shortwave_screen() {
  static const Screen s{orcsdr::dashboards::Id::shortwave, "SHORTWAVE", enter, leave, tick, tap, drag, spectrum};
  return s;
}
}  // namespace screens

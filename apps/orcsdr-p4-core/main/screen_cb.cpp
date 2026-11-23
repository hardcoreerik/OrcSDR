// CB RADIO dashboard (apps/orcsdr-tab5/ui/cb_dashboard.cpp and cb_scanner.cpp, unchanged) on the P4 console.
// The dongle is retuned to the selected channel; the 960 kHz stream covers the whole 40-channel band, so the
// activity monitor and scanner watch every channel from the same spectrum.
#include <algorithm>
#include <cstring>
#include "audio_service.hpp"
#include "cb_dashboard.hpp"
#include "cb_scanner.hpp"
#include "esp_timer.h"
#include "home_app.hpp"
#include "radio_service.hpp"
#include "screens.hpp"

namespace {
namespace cb = orcsdr::cb;
cb::Monitor g_monitor;
cb::Scanner g_scanner;
cb::Mode g_mode = cb::Mode::am;
int32_t g_clarifier = 0;
int32_t g_squelch = -75;
cb::Snapshot g_snapshot;
bool g_first = true;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

void tune_channel(size_t channel, bool from_scanner = false) {
  channel = std::min<size_t>(channel, cb::kChannelCount - 1);
  home_app::request_frequency(cb::kChannelsHz[channel]);
  if (!from_scanner) g_scanner.note_manual_tune(channel);
  if (g_scanner.settings().auto_sideband && g_mode != cb::Mode::usb)
    g_mode = channel >= cb::kFirstSsbChannel ? cb::Mode::lsb : cb::Mode::am;
}

const cb::Snapshot& build() {
  const home_app::Status st = home_app::status();
  const auto rs = orcsdr::radio::snapshot();
  const uint32_t now = now_ms();
  cb::Snapshot& s = g_snapshot;
  s.now_ms = now;
  s.frequency_hz = st.frequency_hz;
  s.channel = static_cast<uint8_t>(cb::nearest_channel(st.frequency_hz));
  s.mode = g_mode;
  s.clarifier_hz = g_clarifier;
  s.squelch_dbfs = g_squelch;
  s.signal_dbfs = orcsdr::radio::level_dbfs();
  s.squelch_open = s.signal_dbfs > g_squelch;
  s.running = rs.receiver_state == 2;
  s.sound_enabled = st.sound;
  s.battery_percent = -1;
  s.scan_state = g_scanner.state();
  s.hang_remaining_ms = g_scanner.hang_remaining_ms(now);
  s.stops = g_scanner.stops();
  s.scan = g_scanner.settings();
  s.floor_db = g_monitor.floor_db();
  s.active_count = static_cast<uint8_t>(g_monitor.active_count());
  for (size_t c = 0; c < cb::kChannelCount; ++c) {
    const cb::ChannelStats& stats = g_monitor.stats(c);
    cb::ChannelView& v = s.channels[c];
    v.snr_db = stats.snr_db;
    v.last_active_ms = stats.last_active_ms;
    v.hits = stats.hits;
    v.active = stats.active;
    v.seen = stats.seen;
    v.locked = g_scanner.locked_out(c);
    v.skipped = g_scanner.skipped(c);
  }
  s.log_count = static_cast<uint8_t>(g_monitor.log_count());
  for (size_t i = 0; i < g_monitor.log_count(); ++i) s.log[i] = *g_monitor.log(i);
  return s;
}

void apply_mode() {
  audio_service::set_mode(g_mode == cb::Mode::usb   ? orcsdr::dsp::Mode::usb
                          : g_mode == cb::Mode::lsb ? orcsdr::dsp::Mode::lsb
                                                    : orcsdr::dsp::Mode::am,
                          true);
}

void enter() {
  const home_app::Status st = home_app::status();
  if (st.frequency_hz < 26900000 || st.frequency_hz > 27450000) tune_channel(cb::kHighwayChannel);
  g_first = true;
  apply_mode();
  cb::enter(build());
}
void leave() { cb::leave(); }
void tick() {
  apply_mode();
  cb::update(build());
}

void tap(int x, int y) {
  const cb::Action a = cb::handle_touch(x, y);
  const size_t current = cb::nearest_channel(home_app::status().frequency_hz);
  cb::Settings& set = g_scanner.settings();
  switch (a.kind) {
    case cb::ActionKind::tune_channel: tune_channel(static_cast<size_t>(std::clamp<int32_t>(a.value, 0, cb::kChannelCount - 1))); break;
    case cb::ActionKind::channel_down: tune_channel((current + cb::kChannelCount - 1) % cb::kChannelCount); break;
    case cb::ActionKind::channel_up: tune_channel((current + 1) % cb::kChannelCount); break;
    case cb::ActionKind::mode_cycle:
      g_mode = g_mode == cb::Mode::am ? cb::Mode::usb : g_mode == cb::Mode::usb ? cb::Mode::lsb : cb::Mode::am;
      break;
    case cb::ActionKind::clarifier_down: g_clarifier = std::max<int32_t>(-1000, g_clarifier - 50); break;
    case cb::ActionKind::clarifier_up: g_clarifier = std::min<int32_t>(1000, g_clarifier + 50); break;
    case cb::ActionKind::squelch_down: g_squelch = std::max<int32_t>(-100, g_squelch - 3); break;
    case cb::ActionKind::squelch_up: g_squelch = std::min<int32_t>(-30, g_squelch + 3); break;
    case cb::ActionKind::scan_toggle:
      if (g_scanner.running()) g_scanner.stop();
      else g_scanner.start(now_ms(), current);
      break;
    case cb::ActionKind::hold_toggle:
      if (g_scanner.state() == cb::State::held) g_scanner.release(now_ms());
      else g_scanner.hold(current);
      break;
    case cb::ActionKind::skip: g_scanner.skip(now_ms()); break;
    case cb::ActionKind::lockout_current:
    case cb::ActionKind::lockout_toggle: {
      const size_t c = a.kind == cb::ActionKind::lockout_current
                           ? current
                           : static_cast<size_t>(std::clamp<int32_t>(a.value, 0, cb::kChannelCount - 1));
      g_scanner.set_lockout(c, !g_scanner.locked_out(c));
      break;
    }
    case cb::ActionKind::lockout_clear: g_scanner.clear_lockouts(); break;
    case cb::ActionKind::lockout_ssb_only: {
      uint64_t mask = 0;
      for (size_t c = 0; c < cb::kFirstSsbChannel; ++c) mask |= uint64_t{1} << c;
      g_scanner.set_lockout_mask(mask);
      break;
    }
    case cb::ActionKind::priority_cycle:
      if (!set.priority_enabled) {
        set.priority_enabled = true;
        set.priority_channel = cb::kEmergencyChannel;
      } else if (set.priority_channel == cb::kEmergencyChannel) {
        set.priority_channel = cb::kHighwayChannel;
      } else {
        set.priority_enabled = false;
      }
      break;
    case cb::ActionKind::threshold_down: set.threshold_db = std::clamp(set.threshold_db - 1.0f, cb::kThresholdMinDb, cb::kThresholdMaxDb); break;
    case cb::ActionKind::threshold_up: set.threshold_db = std::clamp(set.threshold_db + 1.0f, cb::kThresholdMinDb, cb::kThresholdMaxDb); break;
    case cb::ActionKind::hang_down: set.hang_ms = set.hang_ms >= 500 ? set.hang_ms - 500 : 0; break;
    case cb::ActionKind::hang_up: set.hang_ms = std::min<uint16_t>(set.hang_ms + 500, cb::kHangMaxMs); break;
    case cb::ActionKind::max_hold_cycle: {
      constexpr size_t n = std::size(cb::kMaxHoldChoicesS);
      size_t i = 0;
      while (i < n && cb::kMaxHoldChoicesS[i] != set.max_hold_s) ++i;
      set.max_hold_s = cb::kMaxHoldChoicesS[(i + 1) % n];
      break;
    }
    case cb::ActionKind::auto_sideband_toggle: set.auto_sideband = !set.auto_sideband; break;
    case cb::ActionKind::log_clear: g_monitor.clear_log(); break;
    case cb::ActionKind::open_settings: home_app::request_open(orcsdr::dashboards::Id::settings); break;
    case cb::ActionKind::exit_home: home_app::request_home(); break;
    default: break;
  }
}

// `levels` is the full 256-bin fft-shifted spectrum (dB) centred on the tuned frequency.
void spectrum(const float* levels, size_t first, size_t visible, float floor) {
  (void)first;
  (void)visible;
  (void)floor;
  const uint32_t rate = orcsdr::radio::sample_rate();
  const uint32_t center = home_app::status().applied_hz ? home_app::status().applied_hz : home_app::status().frequency_hz;
  cb::draw_spectrum(levels, 256, rate, center);
  float ch[cb::kChannelCount];
  cb::channel_levels(levels, 256, rate, center, ch);
  const uint32_t now = now_ms();
  g_monitor.observe(now, ch, cb::noise_floor(ch), g_scanner.settings().threshold_db);
  const int next = g_scanner.update(now, g_monitor);
  if (next >= 0) tune_channel(static_cast<size_t>(next), true);
}
}  // namespace

namespace screens {
const Screen& cb_screen() {
  static const Screen s{orcsdr::dashboards::Id::cb, "CB", enter, leave, tick, tap, nullptr, spectrum};
  return s;
}
}  // namespace screens

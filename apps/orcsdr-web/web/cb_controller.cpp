// See cb_controller.hpp. Mirrors tune_cb_channel / apply_cb_mode / handle_cb_dashboard_action /
// service_cb_scanner / cb_dashboard_snapshot / persist_cb_settings / load_cb_settings in OrcSDR's main.cpp.
#include "cb_controller.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace cbweb {
namespace {

using namespace orcsdr;
using cb::ActionKind;
using cb::Mode;

constexpr uint32_t kRetuneBlankMs = 120;     // the device blanks 250 ms for a hardware retune; here only the mixer moves
constexpr int32_t kClarifierLimitHz = 1500;
constexpr int32_t kClarifierStepHz = 100;
constexpr int32_t kSquelchOpenDbfs = -90;
constexpr int32_t kSquelchMaxDbfs = -35;
constexpr int32_t kSquelchStepDb = 5;

NvsStore* g_store = nullptr;
cb::Monitor g_monitor;
cb::Scanner g_scanner;
cb::Snapshot g_snapshot;
float g_channel_levels[cb::kChannelCount];
Mode g_mode = Mode::am;
int32_t g_clarifier = 0;
int32_t g_squelch = -75;
uint32_t g_frequency = cb::kChannelsHz[cb::kHighwayChannel];
uint32_t g_pending_tune = 0;
uint32_t g_observe_after_ms = 0;
float g_signal = -90.0f;
bool g_squelch_open = false, g_running = false, g_sound = true;
bool g_scan_resume = false;

void persist() {
  if (!g_store) return;
  const auto& s = g_scanner.settings();
  g_store->put_u8("cb_mode", static_cast<uint8_t>(g_mode));
  g_store->put_i32("cb_clar", g_clarifier);
  g_store->put_i32("cb_sql", g_squelch);
  g_store->put_u8("cb_thresh", static_cast<uint8_t>(lroundf(s.threshold_db)));
  g_store->put_u16("cb_hang", s.hang_ms);
  g_store->put_u16("cb_maxhold", s.max_hold_s);
  g_store->put_u8("cb_pri", s.priority_enabled ? s.priority_channel : static_cast<uint8_t>(0xff));
  g_store->put_bool("cb_autossb", s.auto_sideband);
  const uint64_t lockouts = g_scanner.lockout_mask();
  g_store->put_bytes("cb_lockout", &lockouts, sizeof(lockouts));
}

void apply_mode(Mode mode, bool save) {
  if (g_mode != mode) {
    g_mode = mode;
    g_clarifier = 0;
  }
  if (save) persist();
}

void tune_channel(size_t channel, bool from_scanner, uint32_t now_ms) {
  channel %= cb::kChannelCount;
  const uint32_t frequency = cb::kChannelsHz[channel];
  if (g_scanner.settings().auto_sideband)
    apply_mode(channel >= cb::kFirstSsbChannel ? Mode::lsb : Mode::am, false);
  if (!from_scanner) {
    g_scanner.note_manual_tune(channel);
    if (g_store) g_store->put_u8("cb_channel", static_cast<uint8_t>(channel));
  }
  g_observe_after_ms = now_ms + kRetuneBlankMs;
  g_frequency = frequency;
  g_pending_tune = frequency;
}

}  // namespace

void load(NvsStore& store) {
  g_store = &store;
  const uint8_t mode = store.get_u8("cb_mode", 0);
  g_mode = mode <= static_cast<uint8_t>(Mode::lsb) ? static_cast<Mode>(mode) : Mode::am;
  g_clarifier = std::clamp(store.get_i32("cb_clar", 0), -kClarifierLimitHz, kClarifierLimitHz);
  g_squelch = std::clamp(store.get_i32("cb_sql", -75), kSquelchOpenDbfs, kSquelchMaxDbfs);
  auto& s = g_scanner.settings();
  s.threshold_db = static_cast<float>(std::clamp<int>(store.get_u8("cb_thresh", 10),
                                                      static_cast<int>(cb::kThresholdMinDb),
                                                      static_cast<int>(cb::kThresholdMaxDb)));
  s.hang_ms = std::min<uint16_t>(store.get_u16("cb_hang", 2000), cb::kHangMaxMs);
  const uint16_t max_hold = store.get_u16("cb_maxhold", 0);
  s.max_hold_s = 0;
  for (uint16_t choice : cb::kMaxHoldChoicesS)
    if (choice == max_hold) s.max_hold_s = choice;
  const uint8_t priority = store.get_u8("cb_pri", static_cast<uint8_t>(cb::kEmergencyChannel));
  s.priority_enabled = priority < cb::kChannelCount;
  if (s.priority_enabled) s.priority_channel = priority;
  s.auto_sideband = store.get_bool("cb_autossb", true);
  uint64_t lockouts = 0;
  if (store.get_bytes("cb_lockout", &lockouts, sizeof(lockouts)) == sizeof(lockouts)) g_scanner.set_lockout_mask(lockouts);
  const uint8_t channel = store.get_u8("cb_channel", static_cast<uint8_t>(cb::kHighwayChannel));
  g_frequency = cb::kChannelsHz[channel < cb::kChannelCount ? channel : cb::kHighwayChannel];
  g_scan_resume = store.get_bool("cb_scan", false);
}

void begin(uint32_t now_ms) {
  g_monitor.reset();
  g_observe_after_ms = now_ms + kRetuneBlankMs;
  g_pending_tune = g_frequency;   // the page starts its channel session at the saved channel
  if (g_scan_resume && !g_scanner.running()) g_scanner.start(now_ms, cb::nearest_channel(g_frequency));
}

void set_signal(float signal_dbfs, bool squelch_open, bool running, bool sound_enabled) {
  g_signal = signal_dbfs;
  g_squelch_open = squelch_open;
  g_running = running;
  g_sound = sound_enabled;
}

void observe(uint32_t now_ms, const float* bins, size_t count, uint32_t sps, uint32_t center_hz) {
  if (static_cast<int32_t>(now_ms - g_observe_after_ms) < 0 || !count) return;
  cb::channel_levels(bins, count, sps, center_hz, g_channel_levels);
  g_monitor.observe(now_ms, g_channel_levels, cb::noise_floor(g_channel_levels), g_scanner.settings().threshold_db);
  const size_t tuned = cb::nearest_channel(g_frequency);
  if (g_scanner.running() && g_scanner.channel() != tuned && g_scanner.state() != cb::State::settling)
    g_scanner.note_manual_tune(tuned);
  const int next = g_scanner.update(now_ms, g_monitor);
  if (next >= 0) tune_channel(static_cast<size_t>(next), true, now_ms);
}

const cb::Snapshot& snapshot(uint32_t now_ms) {
  auto& s = g_snapshot;
  s.now_ms = now_ms;
  s.frequency_hz = g_frequency;
  s.channel = static_cast<uint8_t>(cb::nearest_channel(g_frequency));
  s.mode = g_mode;
  s.clarifier_hz = g_clarifier;
  s.squelch_dbfs = g_squelch;
  s.squelch_open = g_squelch_open;
  s.signal_dbfs = g_signal;
  s.running = g_running;
  s.sound_enabled = g_sound;
  s.battery_percent = -1;
  s.scan_state = g_scanner.state();
  s.hang_remaining_ms = g_scanner.hang_remaining_ms(now_ms);
  s.stops = g_scanner.stops();
  s.scan = g_scanner.settings();
  s.floor_db = g_monitor.floor_db();
  s.active_count = static_cast<uint8_t>(g_monitor.active_count());
  for (size_t ch = 0; ch < cb::kChannelCount; ++ch) {
    const auto& st = g_monitor.stats(ch);
    auto& v = s.channels[ch];
    v.snr_db = st.snr_db;
    v.last_active_ms = st.last_active_ms;
    v.hits = st.hits;
    v.active = st.active;
    v.seen = st.seen;
    v.locked = g_scanner.locked_out(ch);
    v.skipped = g_scanner.skipped(ch);
  }
  s.log_count = static_cast<uint8_t>(g_monitor.log_count());
  for (size_t i = 0; i < g_monitor.log_count(); ++i) s.log[i] = *g_monitor.log(i);
  return s;
}

void act(const cb::Action& action, uint32_t now_ms) {
  auto& s = g_scanner.settings();
  const size_t current = cb::nearest_channel(g_frequency);
  switch (action.kind) {
    case ActionKind::none: return;
    case ActionKind::tune_channel:
      tune_channel(static_cast<size_t>(std::clamp<int32_t>(action.value, 0, cb::kChannelCount - 1)), false, now_ms);
      return;
    case ActionKind::channel_down: tune_channel((current + cb::kChannelCount - 1) % cb::kChannelCount, false, now_ms); return;
    case ActionKind::channel_up: tune_channel((current + 1) % cb::kChannelCount, false, now_ms); return;
    case ActionKind::mode_cycle:
      apply_mode(g_mode == Mode::am ? Mode::usb : g_mode == Mode::usb ? Mode::lsb : Mode::am, true);
      return;
    case ActionKind::clarifier_down:
    case ActionKind::clarifier_up:
      g_clarifier = std::clamp(g_clarifier + (action.kind == ActionKind::clarifier_up ? kClarifierStepHz : -kClarifierStepHz),
                               -kClarifierLimitHz, kClarifierLimitHz);
      persist();
      return;
    case ActionKind::squelch_down:
    case ActionKind::squelch_up:
      g_squelch = std::clamp(g_squelch + (action.kind == ActionKind::squelch_up ? kSquelchStepDb : -kSquelchStepDb),
                             kSquelchOpenDbfs, kSquelchMaxDbfs);
      persist();
      return;
    case ActionKind::scan_toggle: {
      const bool enable = !g_scanner.running();
      if (enable) g_scanner.start(now_ms, current);
      else g_scanner.stop();
      g_scan_resume = enable;
      if (g_store) {
        g_store->put_bool("cb_scan", enable);
        if (!enable) g_store->put_u8("cb_channel", static_cast<uint8_t>(current));
      }
      return;
    }
    case ActionKind::hold_toggle:
      if (g_scanner.state() == cb::State::held) g_scanner.release(now_ms);
      else g_scanner.hold(current);
      return;
    case ActionKind::skip: g_scanner.skip(now_ms); return;
    case ActionKind::lockout_current:
    case ActionKind::lockout_toggle: {
      const size_t channel = action.kind == ActionKind::lockout_current
                                 ? current
                                 : static_cast<size_t>(std::clamp<int32_t>(action.value, 0, cb::kChannelCount - 1));
      g_scanner.set_lockout(channel, !g_scanner.locked_out(channel));
      persist();
      return;
    }
    case ActionKind::lockout_clear: g_scanner.clear_lockouts(); persist(); return;
    case ActionKind::lockout_ssb_only: {
      uint64_t mask = 0;
      for (size_t ch = 0; ch < cb::kFirstSsbChannel; ++ch) mask |= uint64_t{1} << ch;
      g_scanner.set_lockout_mask(mask);
      persist();
      return;
    }
    case ActionKind::priority_cycle:   // OFF -> CH 9 -> CH 19 -> OFF
      if (!s.priority_enabled) { s.priority_enabled = true; s.priority_channel = cb::kEmergencyChannel; }
      else if (s.priority_channel == cb::kEmergencyChannel) s.priority_channel = cb::kHighwayChannel;
      else s.priority_enabled = false;
      persist();
      return;
    case ActionKind::threshold_down:
    case ActionKind::threshold_up:
      s.threshold_db = std::clamp(s.threshold_db + (action.kind == ActionKind::threshold_up ? 1.0f : -1.0f),
                                  cb::kThresholdMinDb, cb::kThresholdMaxDb);
      persist();
      return;
    case ActionKind::hang_down: s.hang_ms = s.hang_ms >= 500 ? s.hang_ms - 500 : 0; persist(); return;
    case ActionKind::hang_up: s.hang_ms = std::min<uint16_t>(s.hang_ms + 500, cb::kHangMaxMs); persist(); return;
    case ActionKind::max_hold_cycle: {
      constexpr size_t n = std::size(cb::kMaxHoldChoicesS);
      size_t i = 0;
      while (i < n && cb::kMaxHoldChoicesS[i] != s.max_hold_s) ++i;
      s.max_hold_s = cb::kMaxHoldChoicesS[(i + 1) % n];
      persist();
      return;
    }
    case ActionKind::auto_sideband_toggle: s.auto_sideband = !s.auto_sideband; persist(); return;
    case ActionKind::log_clear: g_monitor.clear_log(); return;
    case ActionKind::open_settings:
    case ActionKind::exit_home: return;   // handled by the caller
  }
}

uint32_t frequency_hz() { return g_frequency; }
int mode() { return static_cast<int>(g_mode); }
int clarifier_hz() { return g_clarifier; }
int squelch_dbfs() { return g_squelch; }
uint32_t take_tune() {
  const uint32_t hz = g_pending_tune;
  g_pending_tune = 0;
  return hz;
}
bool scanning() { return g_scanner.running(); }

}  // namespace cbweb

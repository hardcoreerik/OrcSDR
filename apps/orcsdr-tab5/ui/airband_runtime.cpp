#include "airband_runtime.hpp"

#include "nvs_store.hpp"

#include <esp_attr.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::airband {
namespace {

Hooks g_hooks{};
EXT_RAM_BSS_ATTR Catalog g_catalog;
Scanner g_scanner;
NvsStore g_store;
bool g_store_ready = false;
bool g_loaded = false;
std::atomic<bool> g_audio_open{true};
ChannelSquelch g_squelch;
uint32_t g_saved_frequency_hz = kGuardFrequencyHz;
Location g_catalog_location{};
bool g_catalog_attempted = false;

// Airband gain. A boot (or anything else that resets the driver) brings the tuner AGC back on, which
// raised the noise floor about 8 dB on the bench and hid weak transmissions. The chosen gain is kept
// in NVS and re-applied once the receiver is running; with nothing saved the default is a fixed
// manual gain that gave the best weak-signal SNR in testing.
constexpr int16_t kDefaultManualGainTenthDb = 400;
bool g_gain_pending = true;

// Channel filter width: standard for the raster spacing unless the scope's filter lines were dragged.
uint32_t g_custom_filter_hz = 0;   // 0 = standard

uint32_t effective_filter_hz(Spacing spacing) {
  return g_custom_filter_hz != 0 ? g_custom_filter_hz : orcsdr::airband::filter_bandwidth_hz(spacing);
}

int16_t nearest_gain_step(const LiveState& live, int16_t wanted) {
  if (live.gain_step_count == 0) return wanted;
  int16_t best = live.gain_steps_tenth_db[0];
  for (uint8_t i = 1; i < live.gain_step_count; ++i) {
    const int16_t step = live.gain_steps_tenth_db[i];
    if (std::abs(step - wanted) < std::abs(best - wanted)) best = step;
  }
  return best;
}

bool open_store() {
  if (g_store_ready) return true;
  g_store_ready = g_store.begin("airband", false);
  return g_store_ready;
}

void save_gain(bool automatic, int16_t gain_tenth_db) {
  if (!open_store()) return;
  (void)g_store.put_u8("gain_auto", automatic ? 1 : 0);
  (void)g_store.put_i32("gain_db10", gain_tenth_db);
}

void publish_audio_squelch() {
  g_squelch.set_threshold_db(g_scanner.settings().squelch_db);
  g_audio_open.store(g_squelch.open(), std::memory_order_release);
}

void load_settings_once() {
  if (g_loaded) return;
  g_loaded = true;
  if (!open_store()) {
    publish_audio_squelch();
    return;
  }
  auto& s = g_scanner.settings();
  const uint8_t spacing = g_store.get_u8("spacing", 0);
  s.spacing = spacing == 1 ? Spacing::khz833 : Spacing::khz25;
  const uint8_t source = g_store.get_u8("source", 0);
  s.source = source == 1 ? ScanSource::full_band : ScanSource::airport_bank;
  s.squelch_db =
      static_cast<int16_t>(std::clamp<int32_t>(g_store.get_i32("sql_db", 4), 0, 30));
  s.settle_ms =
      static_cast<uint16_t>(std::clamp<uint32_t>(g_store.get_u16("settle", 350), 300, 800));
  s.hang_ms =
      static_cast<uint16_t>(std::clamp<uint32_t>(g_store.get_u16("hang", 1500), 0, 5000));
  s.priority_guard = g_store.get_bool("guard", true);
  const uint16_t radius = g_store.get_u16("radius", 100);
  s.radius_nm = (radius == 0 || radius == 25 || radius == 50 || radius == 100 ||
                 radius == 250 || radius == 500)
                    ? radius
                    : 100;
  g_custom_filter_hz = std::clamp<uint32_t>(g_store.get_u32("filter_hz", 0), 0u, 20000u);
  if (g_custom_filter_hz != 0 && g_custom_filter_hz < 3000u) g_custom_filter_hz = 0;
  const uint32_t saved = g_store.get_u32("last_hz", kGuardFrequencyHz);
  g_saved_frequency_hz = in_band(saved) ? saved : kGuardFrequencyHz;
  publish_audio_squelch();
}

void save_settings() {
  publish_audio_squelch();
  if (!open_store()) return;
  const auto& s = g_scanner.settings();
  (void)g_store.put_u8("spacing", s.spacing == Spacing::khz833 ? 1 : 0);
  (void)g_store.put_u8("source", s.source == ScanSource::full_band ? 1 : 0);
  (void)g_store.put_i32("sql_db", s.squelch_db);
  (void)g_store.put_u16("settle", s.settle_ms);
  (void)g_store.put_u16("hang", s.hang_ms);
  (void)g_store.put_bool("guard", s.priority_guard);
  (void)g_store.put_u16("radius", s.radius_nm);
}

void save_frequency(uint32_t frequency_hz) {
  if (!in_band(frequency_hz)) return;
  g_saved_frequency_hz = frequency_hz;
  if (open_store()) (void)g_store.put_u32("last_hz", frequency_hz);
}

void rebuild_bank(uint32_t current_frequency_hz) {
  static BankEntry entries[kBankCapacity];
  for (auto& entry : entries) entry = {};
  size_t count = g_catalog.make_bank(entries, kBankCapacity);

  if (count == 0 && in_band(current_frequency_hz)) {
    entries[0].frequency_hz = current_frequency_hz;
    std::strncpy(entries[0].label, "CURRENT FREQUENCY",
                 sizeof(entries[0].label) - 1);
    count = 1;
  }
  g_scanner.set_bank(entries, count);
}

bool same_location(const Location& a, const Location& b) {
  return a.configured == b.configured &&
         (!a.configured ||
          (a.latitude_e7 == b.latitude_e7 && a.longitude_e7 == b.longitude_e7 &&
           a.radius_nm == b.radius_nm));
}

void load_catalog(const LiveState& live) {
  Location requested{live.location_configured, live.latitude_e7, live.longitude_e7};
  requested.radius_nm = g_scanner.settings().radius_nm;
  g_catalog_location = requested;
  g_catalog_attempted = true;
  const int64_t started_us = esp_timer_get_time();
  if (live.filesystem != nullptr)
    (void)g_catalog.load(live.filesystem, requested);
  else
    g_catalog.clear();
  ESP_LOGI("airband", "catalog load elapsed_ms=%lld entries=%u loaded=%d radius_nm=%u",
           static_cast<long long>((esp_timer_get_time() - started_us) / 1000),
           static_cast<unsigned>(g_catalog.count()), g_catalog.loaded() ? 1 : 0,
           static_cast<unsigned>(requested.radius_nm));
  rebuild_bank(live.frequency_hz);
}

const Snapshot& snapshot(const LiveState& live) {
  EXT_RAM_BSS_ATTR static Snapshot out;
  reset_snapshot(out);
  out.now_ms = live.now_ms;
  out.frequency_hz = live.frequency_hz;
  out.channel_db = live.channel_db;
  out.snr_db = g_squelch.snr_db();
  out.floor_db = g_squelch.floor_db();
  out.filter_hz = effective_filter_hz(g_scanner.settings().spacing);
  out.squelch_open = g_squelch.open();
  out.running = live.receiver_running;
  out.sound_enabled = live.sound_enabled;
  out.battery_percent = live.battery_percent;
  out.location_configured = live.location_configured;
  out.controls = live.controls;
  out.gain_step_count = std::min<uint8_t>(live.gain_step_count, 32);
  for (uint8_t i = 0; i < out.gain_step_count; ++i)
    out.gain_steps_tenth_db[i] = live.gain_steps_tenth_db[i];

  if (live.frequency_hz == kGuardFrequencyHz) {
    std::strncpy(out.current_label, "121.500 EMERGENCY / GUARD",
                 sizeof(out.current_label) - 1);
  } else if (const CatalogEntry* entry = g_catalog.match(live.frequency_hz)) {
    std::strncpy(out.current_label, entry->label, sizeof(out.current_label) - 1);
  } else {
    std::strncpy(out.current_label, "AIRBAND AM", sizeof(out.current_label) - 1);
  }

  out.scan_state = g_scanner.state();
  out.scan = g_scanner.settings();
  out.hang_remaining_ms = g_scanner.hang_remaining_ms(live.now_ms);
  out.stops = g_scanner.stops();
  out.channels_checked = g_scanner.channels_checked();

  out.catalog_loaded = g_catalog.loaded();
  out.load_result = g_catalog.last_result();
  out.catalog_count = static_cast<uint8_t>(
      std::min<size_t>(g_catalog.count(), Catalog::kCapacity));
  for (size_t i = 0; i < out.catalog_count; ++i)
    out.catalog[i] = *g_catalog.entry(i);

  out.bank_count =
      static_cast<uint8_t>(std::min<size_t>(g_scanner.bank_count(), kBankCapacity));
  for (size_t i = 0; i < out.bank_count; ++i)
    out.bank[i] = *g_scanner.bank(i);

  out.activity_count =
      static_cast<uint8_t>(std::min<size_t>(g_scanner.activity_count(),
                                            kActivityCapacity));
  for (size_t i = 0; i < out.activity_count; ++i)
    out.activity[i] = *g_scanner.activity(i);
  return out;
}

bool tune(uint32_t frequency_hz) {
  if (!g_hooks.tune) return false;
  frequency_hz = std::clamp(frequency_hz, kMinFrequencyHz, kMaxFrequencyHz);
  if (!g_hooks.tune(frequency_hz)) return false;
  save_frequency(frequency_hz);
  return true;
}

void cycle_settle() {
  auto& value = g_scanner.settings().settle_ms;
  if (value < 325) value = 350;
  else if (value < 400) value = 450;
  else if (value < 525) value = 600;
  else value = 300;
}

void dispatch(const Action& action, const LiveState& live) {
  auto& s = g_scanner.settings();
  switch (action.kind) {
    case ActionKind::none:
      return;
    case ActionKind::tune_down:
      g_scanner.stop();
      (void)tune(step_frequency(live.frequency_hz, -1, s.spacing));
      break;
    case ActionKind::tune_up:
      g_scanner.stop();
      (void)tune(step_frequency(live.frequency_hz, 1, s.spacing));
      break;
    case ActionKind::tune_guard:
      g_scanner.stop();
      (void)tune(kGuardFrequencyHz);
      break;
    case ActionKind::filter_down:
    case ActionKind::filter_up: {
      const uint32_t current = effective_filter_hz(s.spacing);
      const uint32_t next = action.kind == ActionKind::filter_up ? current + 1000u
                                                                : (current > 3000u ? current - 1000u : 3000u);
      dispatch({ActionKind::filter_set, static_cast<int32_t>(std::clamp<uint32_t>(next, 3000u, 20000u))}, live);
      break;
    }
    case ActionKind::filter_set:
      if (action.value >= 3000) {
        g_custom_filter_hz = std::min<uint32_t>(static_cast<uint32_t>(action.value), 20000u);
        if (open_store()) (void)g_store.put_u32("filter_hz", g_custom_filter_hz);
        if (g_hooks.apply_filter) g_hooks.apply_filter(g_custom_filter_hz);
      }
      break;
    case ActionKind::span_down:
    case ActionKind::span_up:
      if (g_hooks.scope_span_step)
        g_hooks.scope_span_step(action.kind == ActionKind::span_up ? 1 : -1);
      break;
    case ActionKind::tune_to:
      if (action.value > 0) {
        g_scanner.stop();
        (void)tune(snap_frequency(static_cast<uint32_t>(action.value), s.spacing));
      }
      break;
    case ActionKind::scan_toggle:
      if (g_scanner.running()) {
        g_scanner.stop();
      } else {
        if (s.source == ScanSource::airport_bank && g_scanner.bank_count() <= 1) {
          s.source = ScanSource::full_band;
          save_settings();
        }
        g_scanner.start(live.now_ms, live.frequency_hz);
      }
      break;
    case ActionKind::hold_toggle:
      if (g_scanner.state() == ScanState::held)
        g_scanner.resume(live.now_ms);
      else
        g_scanner.hold(live.now_ms, live.frequency_hz);
      break;
    case ActionKind::skip:
      g_scanner.skip(live.now_ms, live.frequency_hz);
      break;
    case ActionKind::tune_catalog: {
      g_scanner.stop();
      uint32_t frequency_hz = 0;
      if (action.value < 0) {
        const size_t index = static_cast<size_t>(-action.value - 1);
        if (const Activity* item = g_scanner.activity(index))
          frequency_hz = item->frequency_hz;
      } else if (dashboard_tab() == Tab::scan) {
        if (const BankEntry* item = g_scanner.bank(static_cast<size_t>(action.value)))
          frequency_hz = item->frequency_hz;
      } else {
        if (const CatalogEntry* item = g_catalog.entry(static_cast<size_t>(action.value)))
          frequency_hz = item->frequency_hz;
      }
      if (frequency_hz != 0) (void)tune(frequency_hz);
      break;
    }
    case ActionKind::source_cycle:
      s.source = s.source == ScanSource::airport_bank ? ScanSource::full_band
                                                       : ScanSource::airport_bank;
      save_settings();
      break;
    case ActionKind::spacing_cycle:
      s.spacing = s.spacing == Spacing::khz25 ? Spacing::khz833 : Spacing::khz25;
      save_settings();
      g_custom_filter_hz = 0;
      if (open_store()) (void)g_store.put_u32("filter_hz", 0);
      if (g_hooks.apply_filter) g_hooks.apply_filter(orcsdr::airband::filter_bandwidth_hz(s.spacing));
      g_squelch.reset();
      break;
    case ActionKind::gain_down:
    case ActionKind::gain_up: {
      if (!g_hooks.apply_gain) return;
      const int16_t next = step_gain(live.gain_steps_tenth_db, live.gain_step_count,
                                     live.controls.gain_tenth_db,
                                     action.kind == ActionKind::gain_up ? 1 : -1);
      (void)g_hooks.apply_gain(receiver_controls::action(
          receiver_controls::Control::rf_gain, live.controls, next));
      save_gain(false, next);
      g_squelch.reset();
      break;
    }
    case ActionKind::tuner_agc_toggle:
      if (g_hooks.apply_gain)
        (void)g_hooks.apply_gain(receiver_controls::action(
            receiver_controls::Control::tuner_agc, live.controls));
      // The toggle flips the mode; remember the new one (and the gain it will use if manual).
      save_gain(!live.controls.tuner_agc, live.controls.gain_tenth_db);
      g_squelch.reset();
      break;
    case ActionKind::rtl_agc_toggle:
      if (g_hooks.apply_gain)
        (void)g_hooks.apply_gain(receiver_controls::action(
            receiver_controls::Control::rtl_agc, live.controls));
      g_squelch.reset();
      break;
    case ActionKind::radius_cycle:
      s.radius_nm = next_radius_nm(s.radius_nm);
      save_settings();
      load_catalog(live);
      break;
    case ActionKind::squelch_down:
      s.squelch_db = static_cast<int16_t>(std::max<int>(0, s.squelch_db - 1));
      save_settings();
      break;
    case ActionKind::squelch_up:
      s.squelch_db = static_cast<int16_t>(std::min<int>(30, s.squelch_db + 1));
      save_settings();
      break;
    case ActionKind::settle_cycle:
      cycle_settle();
      save_settings();
      break;
    case ActionKind::hang_down:
      s.hang_ms = static_cast<uint16_t>(s.hang_ms <= 500 ? 0 : s.hang_ms - 500);
      save_settings();
      break;
    case ActionKind::hang_up:
      s.hang_ms = static_cast<uint16_t>(std::min<int>(5000, s.hang_ms + 500));
      save_settings();
      break;
    case ActionKind::priority_toggle:
      s.priority_guard = !s.priority_guard;
      rebuild_bank(live.frequency_hz);
      save_settings();
      break;
    case ActionKind::reload_catalog:
      load_catalog(live);
      break;
    case ActionKind::clear_activity:
      g_scanner.clear_activity();
      break;
    case ActionKind::open_settings:
      if (g_hooks.open_radio_settings) g_hooks.open_radio_settings();
      return;
    case ActionKind::open_location_settings:
      if (g_hooks.open_location_settings) g_hooks.open_location_settings();
      return;
    case ActionKind::exit_home:
      leave();
      if (g_hooks.show_home) g_hooks.show_home();
      return;
  }
}

}  // namespace

void configure(const Hooks& hooks) {
  g_hooks = hooks;
  dashboard_set_scope_span_hook(hooks.scope_span_set);
  load_settings_once();
}

// Applies the saved (or default) Airband gain once the driver is up. Returns true when done.
bool apply_saved_gain(const LiveState& live) {
  if (!g_hooks.apply_gain || !live.receiver_running || !live.controls.capabilities.rf_gain)
    return false;
  const bool saved_auto = open_store() && g_store.get_u8("gain_auto", 0) == 1;
  const int32_t saved_gain =
      open_store() ? g_store.get_i32("gain_db10", kDefaultManualGainTenthDb) : kDefaultManualGainTenthDb;
  if (saved_auto) {
    if (!live.controls.tuner_agc)
      (void)g_hooks.apply_gain(receiver_controls::action(receiver_controls::Control::tuner_agc,
                                                         live.controls));
    return true;
  }
  const int16_t gain = nearest_gain_step(live, static_cast<int16_t>(saved_gain));
  (void)g_hooks.apply_gain(
      receiver_controls::action(receiver_controls::Control::rf_gain, live.controls, gain));
  g_squelch.reset();
  return true;
}

void enter(const LiveState& live) {
  g_gain_pending = true;
  load_settings_once();
  g_squelch.reset();
  publish_audio_squelch();
  Location requested{live.location_configured, live.latitude_e7, live.longitude_e7};
  requested.radius_nm = g_scanner.settings().radius_nm;
  if (!g_catalog_attempted || !same_location(requested, g_catalog_location))
    load_catalog(live);
  else
    rebuild_bank(live.frequency_hz);
  const Snapshot& first = snapshot(live);
  dashboard_enter(first);
}

void leave() {
  g_scanner.stop();
  dashboard_leave();
}

void update(const LiveState& live) {
  if (!dashboard_active()) return;
  dashboard_update(snapshot(live));
}

void redraw() {
  if (dashboard_active()) dashboard_draw();
}

void service(const LiveState& live) {
  if (g_gain_pending && apply_saved_gain(live)) g_gain_pending = false;
  if (live.receiver_running) {
    g_squelch.update(live.now_ms, live.channel_db);
    g_audio_open.store(g_squelch.open(), std::memory_order_release);
  }
  if (!g_scanner.running()) return;
  const uint32_t target =
      g_scanner.service(live.now_ms, live.frequency_hz, g_squelch.snr_db());
  if (target != 0 && target != live.frequency_hz && tune(target))
    g_scanner.note_retuned(live.now_ms, target);
}

void handle_touch(int32_t x, int32_t y, const LiveState& live) {
  if (!dashboard_active()) return;
  dispatch(dashboard_handle_touch(x, y), live);
  if (dashboard_active()) dashboard_update(snapshot(live));
}

bool serial_action(const char* verb, bool has_value, uint32_t value, const LiveState& live) {
  if (!dashboard_active() || verb == nullptr) return false;
  struct Entry { const char* verb; ActionKind kind; };
  static constexpr Entry kVerbs[] = {
      {"UP", ActionKind::tune_up},           {"DOWN", ActionKind::tune_down},
      {"GUARD", ActionKind::tune_guard},     {"SCAN", ActionKind::scan_toggle},
      {"HOLD", ActionKind::hold_toggle},     {"SKIP", ActionKind::skip},
      {"SOURCE", ActionKind::source_cycle},  {"SPACING", ActionKind::spacing_cycle},
      {"RADIUS", ActionKind::radius_cycle},  {"SQUELCH_UP", ActionKind::squelch_up},
      {"SQUELCH_DOWN", ActionKind::squelch_down}, {"SETTLE", ActionKind::settle_cycle},
      {"HANG_UP", ActionKind::hang_up},      {"HANG_DOWN", ActionKind::hang_down},
      {"PRIORITY", ActionKind::priority_toggle}, {"RELOAD", ActionKind::reload_catalog},
      {"CLEAR", ActionKind::clear_activity}, {"GAIN_UP", ActionKind::gain_up},
      {"GAIN_DOWN", ActionKind::gain_down},  {"AGC", ActionKind::tuner_agc_toggle},
      {"RTLAGC", ActionKind::rtl_agc_toggle},
      {"SPAN_UP", ActionKind::span_up},       {"SPAN_DOWN", ActionKind::span_down},
      {"FILTER_UP", ActionKind::filter_up},   {"FILTER_DOWN", ActionKind::filter_down},
  };
  if (std::strcmp(verb, "TUNE") == 0) {
    if (!has_value || !in_band(value)) return false;
    g_scanner.stop();
    return tune(value);
  }
  if (std::strcmp(verb, "FILTER") == 0) {   // channel filter width in Hz (3000-20000); 0 = standard
    if (!has_value || (value != 0 && (value < 3000 || value > 20000))) return false;
    if (value == 0) {
      g_custom_filter_hz = 0;
      if (open_store()) (void)g_store.put_u32("filter_hz", 0);
      if (g_hooks.apply_filter) g_hooks.apply_filter(orcsdr::airband::filter_bandwidth_hz(g_scanner.settings().spacing));
    } else {
      dispatch({ActionKind::filter_set, static_cast<int32_t>(value)}, live);
    }
    dashboard_update(snapshot(live));
    return true;
  }
  if (std::strcmp(verb, "SQUELCH") == 0) {
    if (!has_value || value > 30) return false;
    g_scanner.settings().squelch_db = static_cast<int16_t>(value);
    save_settings();
    dashboard_update(snapshot(live));
    return true;
  }
  if (std::strcmp(verb, "TAB") == 0) {
    if (!has_value || value > 5) return false;
    dashboard_select_tab(static_cast<Tab>(value));
    return true;
  }
  for (const Entry& entry : kVerbs) {
    if (std::strcmp(verb, entry.verb) != 0) continue;
    dispatch(Action{entry.kind, 0}, live);
    if (dashboard_active()) dashboard_update(snapshot(live));
    return true;
  }
  return false;
}

size_t status_line(const LiveState& live, char* out, size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  load_settings_once();
  const Settings& s = g_scanner.settings();
  const CatalogEntry* match = g_catalog.match(live.frequency_hz);
  const int written = std::snprintf(
      out, capacity,
      "RTL_AIRBAND_STATUS active=%d tab=%u frequency_hz=%lu scan=%s squelch_open=%d "
      "level_db=%.1f snr_db=%.1f floor_db=%.1f sql_db=%d spacing=%s source=%s radius_nm=%u filter_hz=%lu "
      "stops=%lu checked=%lu bank=%u activity=%u catalog=%u loaded=%d location=%d "
      "gain_tenth_db=%d tuner_agc=%d rtl_agc=%d running=%d load=%s match=%s scope_fps=%lu scope_ms=%lu",
      dashboard_active() ? 1 : 0, static_cast<unsigned>(dashboard_tab()),
      static_cast<unsigned long>(live.frequency_hz), state_name(g_scanner.state()),
      g_squelch.open() ? 1 : 0, static_cast<double>(live.channel_db),
      static_cast<double>(g_squelch.snr_db()), static_cast<double>(g_squelch.floor_db()),
      static_cast<int>(s.squelch_db), spacing_name(s.spacing), source_name(s.source),
      static_cast<unsigned>(s.radius_nm),
      static_cast<unsigned long>(effective_filter_hz(s.spacing)),
      static_cast<unsigned long>(g_scanner.stops()),
      static_cast<unsigned long>(g_scanner.channels_checked()),
      static_cast<unsigned>(g_scanner.bank_count()),
      static_cast<unsigned>(g_scanner.activity_count()),
      static_cast<unsigned>(g_catalog.count()), g_catalog.loaded() ? 1 : 0,
      live.location_configured ? 1 : 0, static_cast<int>(live.controls.gain_tenth_db),
      live.controls.tuner_agc ? 1 : 0, live.controls.rtl_agc ? 1 : 0,
      live.receiver_running ? 1 : 0, load_result_name(g_catalog.last_result()),
      match != nullptr ? match->airport_ident : "none",
      static_cast<unsigned long>(dashboard_scope_fps()),
      static_cast<unsigned long>(dashboard_scope_draw_ms()));
  return written < 0 ? 0 : static_cast<size_t>(written);
}

bool active() { return dashboard_active(); }
bool spectrum_active() { return dashboard_spectrum_active(); }
void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, uint32_t span_hz) {
  dashboard_draw_spectrum(levels, first_bin, visible_bins, span_hz);
}
Tab tab() { return dashboard_tab(); }

bool audio_open() { return g_audio_open.load(std::memory_order_acquire); }

uint32_t default_frequency() {
  load_settings_once();
  return g_saved_frequency_hz;
}

uint32_t filter_bandwidth_hz() {
  load_settings_once();
  return effective_filter_hz(g_scanner.settings().spacing);
}

bool scope_edge_hit(int32_t x, int32_t y) { return dashboard_scope_edge_hit(x, y); }

void scope_drag_filter(int32_t x, const LiveState& live) {
  const uint32_t hz = dashboard_scope_filter_from_x(x);
  if (hz == 0) return;
  dispatch({ActionKind::filter_set, static_cast<int32_t>(hz)}, live);
  if (dashboard_active()) dashboard_update(snapshot(live));
}

uint32_t manual_step(uint32_t frequency_hz, int direction) {
  load_settings_once();
  return step_frequency(frequency_hz, direction, g_scanner.settings().spacing);
}

const Settings& settings() {
  load_settings_once();
  return g_scanner.settings();
}

bool runtime_self_check() {
  load_settings_once();
  return Scanner::self_check() && Catalog::self_check() &&
         dashboard_self_check() && in_band(default_frequency());
}

}  // namespace orcsdr::airband

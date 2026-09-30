#include "airband_runtime.hpp"

#include "nvs_store.hpp"

#include <esp_attr.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::airband {
namespace {

Hooks g_hooks{};
EXT_RAM_BSS_ATTR Catalog g_catalog;
Scanner g_scanner;
NvsStore g_store;
bool g_store_ready = false;
bool g_loaded = false;
std::atomic<int16_t> g_audio_squelch_dbfs{-75};
uint32_t g_saved_frequency_hz = kGuardFrequencyHz;
Location g_catalog_location{};

bool open_store() {
  if (g_store_ready) return true;
  g_store_ready = g_store.begin("airband", false);
  return g_store_ready;
}

void publish_audio_squelch() {
  g_audio_squelch_dbfs.store(g_scanner.settings().squelch_dbfs,
                             std::memory_order_release);
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
  s.squelch_dbfs =
      static_cast<int16_t>(std::clamp(g_store.get_i32("squelch", -75), -100, -30));
  s.settle_ms =
      static_cast<uint16_t>(std::clamp<uint32_t>(g_store.get_u16("settle", 350), 300, 800));
  s.hang_ms =
      static_cast<uint16_t>(std::clamp<uint32_t>(g_store.get_u16("hang", 1500), 0, 5000));
  s.priority_guard = g_store.get_bool("guard", true);
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
  (void)g_store.put_i32("squelch", s.squelch_dbfs);
  (void)g_store.put_u16("settle", s.settle_ms);
  (void)g_store.put_u16("hang", s.hang_ms);
  (void)g_store.put_bool("guard", s.priority_guard);
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
          (a.latitude_e7 == b.latitude_e7 && a.longitude_e7 == b.longitude_e7));
}

void load_catalog(const LiveState& live) {
  const Location requested{live.location_configured, live.latitude_e7, live.longitude_e7};
  g_catalog_location = requested;
  if (live.filesystem != nullptr)
    (void)g_catalog.load(live.filesystem, requested);
  else
    g_catalog.clear();
  rebuild_bank(live.frequency_hz);
}

const Snapshot& snapshot(const LiveState& live) {
  EXT_RAM_BSS_ATTR static Snapshot out;
  out = {};
  out.now_ms = live.now_ms;
  out.frequency_hz = live.frequency_hz;
  out.signal_dbfs = live.signal_dbfs;
  out.squelch_open = audio_open(live.signal_dbfs);
  out.running = live.receiver_running;
  out.sound_enabled = live.sound_enabled;
  out.battery_percent = live.battery_percent;
  out.location_configured = live.location_configured;

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
      break;
    case ActionKind::squelch_down:
      s.squelch_dbfs = static_cast<int16_t>(std::max<int>(-100, s.squelch_dbfs - 3));
      save_settings();
      break;
    case ActionKind::squelch_up:
      s.squelch_dbfs = static_cast<int16_t>(std::min<int>(-30, s.squelch_dbfs + 3));
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
  load_settings_once();
}

void enter(const LiveState& live) {
  load_settings_once();
  const Location requested{live.location_configured, live.latitude_e7,
                           live.longitude_e7};
  if (!g_catalog.loaded() || !same_location(requested, g_catalog_location))
    load_catalog(live);
  else
    rebuild_bank(live.frequency_hz);
  dashboard_enter(snapshot(live));
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
  if (!g_scanner.running()) return;
  const uint32_t target =
      g_scanner.service(live.now_ms, live.frequency_hz, live.signal_dbfs);
  if (target != 0 && target != live.frequency_hz && tune(target))
    g_scanner.note_retuned(live.now_ms, target);
}

void handle_touch(int32_t x, int32_t y, const LiveState& live) {
  if (!dashboard_active()) return;
  dispatch(dashboard_handle_touch(x, y), live);
  if (dashboard_active()) dashboard_update(snapshot(live));
}

bool active() { return dashboard_active(); }
Tab tab() { return dashboard_tab(); }

bool audio_open(float signal_dbfs) {
  const int16_t threshold = g_audio_squelch_dbfs.load(std::memory_order_acquire);
  return threshold <= -100 || signal_dbfs >= static_cast<float>(threshold);
}

uint32_t default_frequency() {
  load_settings_once();
  return g_saved_frequency_hz;
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

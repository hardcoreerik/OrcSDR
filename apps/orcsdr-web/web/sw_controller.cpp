// See sw_controller.hpp. Mirrors shortwave_dashboard_snapshot / handle_shortwave_dashboard_action in OrcSDR's
// main.cpp (and the tune/step/filter helpers they use).
#include "sw_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "orcsdr_storage.hpp"
#include "receiver_band_plan.hpp"
#include "shortwave_audio_dsp.hpp"
#include "shortwave_hunt.hpp"
#include "shortwave_library.hpp"
#include "shortwave_ssb.hpp"

namespace swweb {
namespace {

using namespace orcsdr;
namespace sw = orcsdr::shortwave;
using sw::ActionKind;
namespace ssb = orcsdr::shortwave_ssb;

constexpr uint32_t kSpanMinHz = 60000;     // the page's window is 300 kS/s, so the scope zoom stops at 240 kHz
constexpr uint32_t kSpanMaxHz = 240000;

NvsStore* g_store = nullptr;
sw::Snapshot g_snapshot;
sw::LibraryState g_library;
bool g_library_loaded = false;
sw::Hunt g_hunt;
uint32_t g_frequency = receiver_bands::kShortwave.default_hz;
uint32_t g_step = receiver_bands::kShortwave.default_step_hz;
uint32_t g_filter = receiver_bands::kShortwave.default_bandwidth_hz;
uint32_t g_span = 240000;
bool g_boost = false;
uint32_t g_pending_tune = 0;
bool g_hunt_active = false;
uint16_t g_hunt_step = 0, g_hunt_total = 0;
int g_hunt_request = -1;
uint32_t g_hunt_restore = 0;

// Inputs from the page.
float g_rf_dbfs = -90.0f, g_clipping = 0.0f;
bool g_running = false, g_driver_ready = false, g_sound = true;
bool g_tuner_agc = true, g_rtl_agc = false;
bool g_cap_gain = false, g_cap_tuner_agc = false, g_cap_rtl_agc = false;
int g_route = 0;
int g_gain_tenth = 0;
uint8_t g_volume = 0;
int32_t g_battery = -1;
uint64_t g_utc = 0;
char g_device[48]{};
int g_gain_steps[32]{};
uint8_t g_gain_step_count = 0;

void persist() {
  if (!g_store) return;
  g_store->put_u32("sw_hz", g_frequency);
  g_store->put_u32("sw_step", g_step);
  g_store->put_u32("sw_bw", g_filter);
  g_store->put_u32("sw_span", g_span);
  g_store->put_bool("sw_boost", g_boost);
}

void ensure_library() {
  if (g_library_loaded) return;
  g_library_loaded = true;
  char error[96]{};
  storage::FileSystem& fs = storage::filesystem();
  if (!sw::load_library(fs, &g_library, error, sizeof(error))) g_library.status = sw::StorageStatus::read_failed;
}

uint32_t clamp_frequency(uint32_t hz) {
  return std::clamp(hz, receiver_bands::kShortwave.min_hz, receiver_bands::kShortwave.max_hz);
}

uint32_t clamp_filter(uint32_t hz) {
  const ssb::Mode mode = ssb::current(g_frequency);
  const bool is_ssb = mode != ssb::Mode::am;
  const uint32_t low = is_ssb ? 2400 : 3000, high = is_ssb ? 12000 : 30000;
  return std::clamp((hz / 1000u) * 1000u, low, high);
}

void tune(uint32_t hz) {
  g_frequency = clamp_frequency(hz);
  sw::note_tuned(g_frequency);
  g_pending_tune = g_frequency;
  persist();
}

uint32_t step_frequency(int direction) {
  if (direction < 0) return g_frequency <= g_step ? clamp_frequency(0) : clamp_frequency(g_frequency - g_step);
  return clamp_frequency(g_frequency + g_step);
}

// Saves a changed record table; on failure puts the table back, as the device does.
template <typename Table, typename Save>
bool commit(Table&, Save save) {
  char error[96]{};
  if (!save(error, sizeof(error))) { g_library.status = sw::StorageStatus::write_failed; return false; }
  g_library.status = sw::StorageStatus::ready;
  return true;
}

}  // namespace

void load(NvsStore& store) {
  g_store = &store;
  g_frequency = clamp_frequency(store.get_u32("sw_hz", g_frequency));
  g_step = store.get_u32("sw_step", g_step);
  g_filter = store.get_u32("sw_bw", g_filter);
  g_span = std::clamp(store.get_u32("sw_span", g_span), kSpanMinHz, kSpanMaxHz);
  g_boost = store.get_bool("sw_boost", false);
  sw::note_tuned(g_frequency);
}

void begin(uint32_t) {
  ensure_library();
  g_pending_tune = g_frequency;   // the page starts its channel session at the saved frequency
}

void set_gain_step(int index, int tenth_db) {
  if (index >= 0 && index < 32) g_gain_steps[index] = tenth_db;
}

void set_num(const char* key, double v) {
#define F(name, var) if (!strcmp(key, name)) { var = static_cast<decltype(var)>(v); return; }
  F("rf_dbfs", g_rf_dbfs) F("clipping", g_clipping) F("running", g_running) F("driver_ready", g_driver_ready)
  F("sound", g_sound) F("tuner_agc", g_tuner_agc) F("rtl_agc", g_rtl_agc) F("cap_gain", g_cap_gain)
  F("cap_tuner_agc", g_cap_tuner_agc) F("cap_rtl_agc", g_cap_rtl_agc) F("route", g_route)
  F("gain_tenth_db", g_gain_tenth) F("volume", g_volume) F("battery", g_battery) F("utc", g_utc)
  F("gain_step_count", g_gain_step_count)
#undef F
}

void set_str(const char* key, const char* v) {
  if (!strcmp(key, "device")) { strncpy(g_device, v, sizeof(g_device) - 1); g_device[sizeof(g_device) - 1] = 0; }
}

const sw::Snapshot& snapshot() {
  ensure_library();
  auto& s = g_snapshot;
  s.frequency_hz = g_frequency;
  s.step_hz = g_step;
  s.filter_bandwidth_hz = g_filter;
  s.span_hz = g_span;
  s.relative_dbfs = g_rf_dbfs;
  s.clipping_percent = g_clipping;
  s.running = g_running;
  s.driver_ready = g_driver_ready;
  s.sound_enabled = g_sound;
  s.dsp = sw::audio_dsp::settings();
  s.dsp_metrics = sw::audio_dsp::metrics();
  const ssb::Mode mode = ssb::current(g_frequency);
  strncpy(s.mode, ssb::mode_name(mode), sizeof(s.mode) - 1);
  s.mode_auto = ssb::control().choice.load(std::memory_order_relaxed) == 0;
  s.clarifier_hz = static_cast<int16_t>(ssb::control().clarifier_hz.load(std::memory_order_relaxed));
  if (mode != ssb::Mode::am) s.filter_bandwidth_hz = ssb::kPassbandHz;
  s.battery_percent = g_battery;
  s.controls.audio_boost = g_boost;
  s.controls.volume = g_volume;
  s.memories = &g_library.memories;
  s.logs = &g_library.logs;
  s.memory_count = static_cast<uint16_t>(g_library.memories.size());
  s.log_count = static_cast<uint16_t>(g_library.logs.size());
  s.storage_status = g_library.status;
  s.utc_valid = g_utc > 1700000000ull;
  if (s.utc_valid) {
    const time_t utc = static_cast<time_t>(g_utc);
    struct tm t{};
    if (gmtime_r(&utc, &t)) {
      s.utc_minute = static_cast<uint16_t>(t.tm_hour * 60 + t.tm_min);
      s.utc_weekday = static_cast<uint8_t>(t.tm_wday);
    } else s.utc_valid = false;
  }
  s.hunt_active = g_hunt_active;
  s.hunt_step = g_hunt_step;
  s.hunt_total = g_hunt_total;
  s.hunt_candidate_count = static_cast<uint8_t>(std::min(g_hunt.candidate_count(), std::size(s.hunt_candidates)));
  for (size_t i = 0; i < s.hunt_candidate_count; ++i) s.hunt_candidates[i] = *g_hunt.candidate(i);
  s.controls.route = g_route == 1 ? sw::ReceiverRoute::direct_q
                    : g_route == 2 ? sw::ReceiverRoute::hf_upconverter
                    : g_route == 3 ? sw::ReceiverRoute::tuner : sw::ReceiverRoute::unknown;
  s.controls.capabilities.rf_gain = g_cap_gain;
  s.controls.capabilities.tuner_agc = g_cap_tuner_agc;
  s.controls.capabilities.rtl_agc = g_cap_rtl_agc;
  s.controls.tuner_agc = g_tuner_agc;
  s.controls.rtl_agc = g_rtl_agc;
  s.controls.gain_tenth_db = static_cast<int16_t>(g_gain_tenth);
  memcpy(s.gain_steps_tenth_db, g_gain_steps, sizeof(g_gain_steps));
  s.gain_step_count = g_gain_step_count;
  strncpy(s.device, g_device, sizeof(s.device) - 1);
  return s;
}

bool act(const sw::Action& action, uint32_t) {
  ensure_library();
  auto& memories = g_library.memories;
  auto& logs = g_library.logs;
  storage::FileSystem& fs = storage::filesystem();
  switch (action.kind) {
    case ActionKind::none: return false;
    case ActionKind::tune_hz: tune(static_cast<uint32_t>(action.value)); return false;
    case ActionKind::open_frequency: return false;
    case ActionKind::step_down: tune(step_frequency(-1)); return false;
    case ActionKind::step_up: tune(step_frequency(1)); return false;
    case ActionKind::step_cycle: g_step = sw::next_tuning_step(g_step); persist(); return false;
    case ActionKind::filter_cycle:
      g_filter = g_filter <= 4000u ? 6000u : g_filter <= 6000u ? 9000u : 4000u;
      persist();
      return false;
    case ActionKind::filter_down:
      g_filter = g_filter <= 6000u ? 4000u : 6000u;
      persist();
      return false;
    case ActionKind::filter_up:
      g_filter = g_filter < 6000u ? 6000u : 9000u;
      persist();
      return false;
    case ActionKind::filter_bandwidth_hz: g_filter = clamp_filter(static_cast<uint32_t>(action.value)); persist(); return false;
    case ActionKind::span_down: g_span = std::max(kSpanMinHz, g_span / 2); persist(); return false;
    case ActionKind::span_up: g_span = std::min(kSpanMaxHz, g_span * 2); persist(); return false;
    case ActionKind::span_hz: g_span = std::clamp(static_cast<uint32_t>(action.value), kSpanMinHz, kSpanMaxHz); persist(); return false;
    case ActionKind::sound_toggle:
    case ActionKind::volume_down:
    case ActionKind::volume_up:
    case ActionKind::gain_auto:
    case ActionKind::gain_tenth_db:
    case ActionKind::rtl_agc:
    case ActionKind::open_settings:
    case ActionKind::exit_home: return true;
    case ActionKind::mode_cycle: {
      const auto next = ssb::next_choice(static_cast<ssb::Choice>(ssb::control().choice.load(std::memory_order_relaxed)));
      ssb::control().choice.store(static_cast<uint8_t>(next), std::memory_order_relaxed);
      return false;
    }
    case ActionKind::clarifier_down:
    case ActionKind::clarifier_up: {
      const int32_t step = action.kind == ActionKind::clarifier_up ? ssb::kClarifierStepHz : -ssb::kClarifierStepHz;
      ssb::control().clarifier_hz.store(ssb::clamp_clarifier(ssb::control().clarifier_hz.load(std::memory_order_relaxed) + step),
                                        std::memory_order_relaxed);
      return false;
    }
    case ActionKind::audio_boost: g_boost = action.value != 0; persist(); return false;
    case ActionKind::clean_audio:
      g_filter = 6000;
      g_boost = false;
      sw::audio_dsp::apply_clean_preset();
      sw::audio_dsp::reset();
      persist();
      return false;
    case ActionKind::noise_reduction_cycle: sw::audio_dsp::cycle_noise_reduction(); return false;
    case ActionKind::auto_notch_toggle: sw::audio_dsp::toggle_auto_notch(); return false;
    case ActionKind::squelch_cycle: sw::audio_dsp::cycle_squelch(); return false;
    case ActionKind::squelch_down: sw::audio_dsp::adjust_squelch(-5); return false;
    case ActionKind::squelch_up: sw::audio_dsp::adjust_squelch(5); return false;
    case ActionKind::hunt_start:
      if (action.value >= 0 && static_cast<size_t>(action.value) < sw::band_count()) {
        g_hunt.clear();
        g_hunt_active = true;
        g_hunt_step = 0;
        const sw::BroadcastBand* band = sw::band(static_cast<size_t>(action.value));
        g_hunt_total = static_cast<uint16_t>((band->max_hz - band->min_hz) / 5000 + 1);
        g_hunt_restore = g_frequency;
        g_hunt_request = static_cast<int>(action.value);
      }
      return false;
    case ActionKind::hunt_cancel:
      g_hunt_active = false;
      g_hunt_request = -2;   // tells the page to stop and restore
      return false;
    case ActionKind::save_memory: {
      sw::Memory memory{};
      memory.frequency_hz = g_frequency;
      memory.bandwidth_hz = g_filter;
      memory.saved_utc = g_utc;
      strncpy(memory.mode, ssb::mode_name(ssb::current(g_frequency)), sizeof(memory.mode) - 1);
      strncpy(memory.station, sw::pending_memory_label(), sizeof(memory.station) - 1);
      strncpy(memory.notes, sw::pending_memory_notes(), sizeof(memory.notes) - 1);
      const auto result = memories.upsert(memory);
      if (result == sw::RecordResult::ok) {
        if (!commit(memories, [&](char* e, size_t n) { return sw::save_memories(fs, memories, e, n); })) memories.discard_last();
      } else if (result != sw::RecordResult::duplicate) g_library.status = sw::StorageStatus::write_failed;
      return false;
    }
    case ActionKind::update_memory:
    case ActionKind::favorite_memory:
    case ActionKind::delete_memory: {
      const size_t index = action.value < 0 ? SIZE_MAX : static_cast<size_t>(action.value);
      sw::Memory before{};
      const auto* existing = memories.at(index);
      const bool had = existing != nullptr;
      if (existing) before = *existing;
      auto result = sw::RecordResult::missing;
      if (action.kind == ActionKind::update_memory) {
        if (existing) {
          auto edited = *existing;
          strncpy(edited.station, sw::pending_memory_label(), sizeof(edited.station) - 1);
          strncpy(edited.notes, sw::pending_memory_notes(), sizeof(edited.notes) - 1);
          result = memories.replace(index, edited);
        }
      } else if (action.kind == ActionKind::favorite_memory) result = memories.toggle_favorite(index);
      else result = memories.erase(index);
      if (result == sw::RecordResult::ok) {
        if (!commit(memories, [&](char* e, size_t n) { return sw::save_memories(fs, memories, e, n); }) && had) {
          if (action.kind == ActionKind::delete_memory) memories.insert(index, before);
          else memories.replace(index, before);
        }
      }
      return false;
    }
    case ActionKind::save_log: {
      if (g_utc < 1700000000ull) return false;   // the logbook needs a real UTC time
      sw::LogEntry entry{};
      entry.timestamp_utc = g_utc;
      entry.frequency_hz = g_frequency;
      entry.bandwidth_hz = g_filter;
      entry.signal_dbfs = g_rf_dbfs;
      strncpy(entry.mode, ssb::mode_name(ssb::current(g_frequency)), sizeof(entry.mode) - 1);
      strncpy(entry.device, g_device, sizeof(entry.device) - 1);
      strncpy(entry.antenna, sw::pending_log_antenna()[0] ? sw::pending_log_antenna() : "Not recorded", sizeof(entry.antenna) - 1);
      strncpy(entry.notes, sw::pending_log_notes(), sizeof(entry.notes) - 1);
      for (size_t i = 0; i < memories.size(); ++i) {
        const auto* m = memories.at(i);
        if (m && m->frequency_hz == entry.frequency_hz) { strncpy(entry.station, m->station, sizeof(entry.station) - 1); break; }
      }
      if (logs.append(entry) == sw::RecordResult::ok &&
          !commit(logs, [&](char* e, size_t n) { return sw::save_logs(fs, logs, e, n); }))
        logs.discard_last();
      return false;
    }
    case ActionKind::update_log:
    case ActionKind::delete_log: {
      const size_t index = action.value < 0 ? SIZE_MAX : static_cast<size_t>(action.value);
      sw::LogEntry before{};
      const auto* existing = logs.at(index);
      const bool had = existing != nullptr;
      if (existing) before = *existing;
      auto result = sw::RecordResult::missing;
      if (action.kind == ActionKind::update_log) {
        if (existing) {
          auto edited = *existing;
          strncpy(edited.antenna, sw::pending_log_antenna(), sizeof(edited.antenna) - 1);
          strncpy(edited.notes, sw::pending_log_notes(), sizeof(edited.notes) - 1);
          result = logs.replace(index, edited);
        }
      } else result = logs.erase(index);
      if (result == sw::RecordResult::ok) {
        if (!commit(logs, [&](char* e, size_t n) { return sw::save_logs(fs, logs, e, n); }) && had) {
          if (action.kind == ActionKind::delete_log) logs.insert(index, before);
          else logs.replace(index, before);
        }
      }
      return false;
    }
    case ActionKind::export_log: {
      char error[96]{};
      (void)sw::export_logs(fs, logs, error, sizeof(error));
      return true;   // the page offers the exported files for download
    }
  }
  return false;
}

uint32_t frequency_hz() { return g_frequency; }
int mode() { return static_cast<int>(ssb::current(g_frequency)); }
int clarifier_hz() { return ssb::control().clarifier_hz.load(std::memory_order_relaxed); }
uint32_t filter_hz() { return ssb::current(g_frequency) != ssb::Mode::am ? ssb::kPassbandHz : g_filter; }
uint32_t span_hz() { return g_span; }
bool audio_boost() { return g_boost; }
uint32_t take_tune() {
  const uint32_t hz = g_pending_tune;
  g_pending_tune = 0;
  return hz;
}

bool hunt_band(size_t index, uint32_t* min_hz, uint32_t* max_hz) {
  const sw::BroadcastBand* band = sw::band(index);
  if (!band) return false;
  *min_hz = band->min_hz;
  *max_hz = band->max_hz;
  return true;
}

// Candidates on the same 5 kHz grid as the device's sweep: the strongest spectrum bin within +/-2.5 kHz of each step.
void hunt_finish(const float* bins, size_t count, uint32_t sps, uint32_t center_hz) {
  g_hunt_active = false;
  if (g_hunt_request < 0 && !g_hunt_total) return;
  const uint32_t lo = center_hz - sps / 2;
  const double hz_per_bin = static_cast<double>(sps) / count;
  // Hunt band = the band the page sent us to; find it again from the centre.
  const sw::BroadcastBand* band = sw::band_for(center_hz);
  if (!band) return;
  for (uint32_t f = band->min_hz; f <= band->max_hz; f += 5000) {
    float peak = -120.0f;
    const double a = (static_cast<double>(f) - 2500.0 - lo) / hz_per_bin, b = (static_cast<double>(f) + 2500.0 - lo) / hz_per_bin;
    for (long i = std::max(0l, static_cast<long>(std::floor(a))); i <= std::min<long>(count - 1, static_cast<long>(std::ceil(b))); ++i)
      peak = std::max(peak, bins[i]);
    if (peak > -120.0f) g_hunt.observe(f, peak);
    ++g_hunt_step;
  }
  g_hunt_step = g_hunt_total;
}

bool hunt_active() { return g_hunt_active; }
int hunt_requested_band() {
  const int r = g_hunt_request;
  g_hunt_request = -1;
  return r;
}

}  // namespace swweb

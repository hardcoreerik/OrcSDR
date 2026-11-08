// SHORTWAVE dashboard (apps/orcsdr-tab5/ui/shortwave_dashboard.cpp and its model, unchanged) on the P4 console.
// AM, USB and LSB are demodulated and the memories and logbook use the SD card (same model and CSV files as the
// Tab5); the audio clean-up chain and band hunt are not available on this target yet.
#include <algorithm>
#include "M5Unified.h"
#include <cstring>
#include <ctime>
#include <new>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "audio_service.hpp"
#include "home_app.hpp"
#include "network_clock.hpp"
#include "orcsdr_storage.hpp"
#include "radio_service.hpp"
#include "screens.hpp"
#include "present.hpp"

namespace screens { bool shortwave_resize_viewport(); }
#include "shortwave_dashboard.hpp"
#include "shortwave_library.hpp"

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
const char* const kTag = "SW_LIB";
// Memories and logbook (CSV on the SD card, shared model and reader/writer with the Tab5).
sw::LibraryState& library() { return psram_table<sw::LibraryState>(); }
bool g_library_loaded = false;
void ensure_library() {
  if (g_library_loaded) return;
  if (!orcsdr::storage::mounted()) {  // the card mounts in the background a few seconds after boot
    library().status = sw::StorageStatus::unavailable;
    return;
  }
  char error[96]{};
  g_library_loaded = true;
  if (!sw::load_library(orcsdr::storage::filesystem(), &library(), error, sizeof(error)))
    ESP_LOGW(kTag, "load_failed error=%s", error);
  else
    ESP_LOGI(kTag, "ready memories=%u logs=%u invalid=%u", unsigned(library().memories.size()),
             unsigned(library().logs.size()), unsigned(library().invalid_rows));
}
bool persist_memories(char* error, size_t capacity) {
  return orcsdr::storage::mounted() && sw::save_memories(orcsdr::storage::filesystem(), library().memories, error, capacity);
}
bool persist_logs(char* error, size_t capacity) {
  return orcsdr::storage::mounted() && sw::save_logs(orcsdr::storage::filesystem(), library().logs, error, capacity);
}

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
  s.battery_percent = M5.Power.getBatteryLevel();
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
  ensure_library();
  s.memories = &library().memories;
  s.logs = &library().logs;
  s.memory_count = static_cast<uint16_t>(library().memories.size());
  s.log_count = static_cast<uint16_t>(library().logs.size());
  s.storage_status = library().status;
  // ON AIR needs the UTC minute and weekday. The P4 clock is network_clock (NTP, GPS, battery-backed RTC); this was
  // hard-coded invalid, so ON AIR always said "SET UTC" even with a synced clock.
  s.utc_valid = network_clock::status().wallclock_valid;
  if (s.utc_valid) {
    const time_t utc = time(nullptr);
    struct tm broken_down{};
    if (gmtime_r(&utc, &broken_down) != nullptr) {
      s.utc_minute = static_cast<uint16_t>(broken_down.tm_hour * 60 + broken_down.tm_min);
      s.utc_weekday = static_cast<uint8_t>(broken_down.tm_wday);
    } else {
      s.utc_valid = false;
    }
  }
  std::snprintf(s.mode, sizeof(s.mode), "%s", mode_text());
  s.mode_auto = g_mode == 0;
  s.clarifier_hz = g_clarifier;
  s.audio_dsp_available = false;
  s.hunt_available = false;
  s.storage_available = orcsdr::storage::mounted();
  return s;
}

void enter() {
  (void)screens::shortwave_resize_viewport();
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

// Memory and logbook actions. Same record rules as the Tab5: a change is kept only if the card write succeeds,
// otherwise it is rolled back and the status shows a write failure.
void library_action(const sw::Action& a) {
  ensure_library();
  sw::LibraryState& lib = library();
  const home_app::Status st = home_app::status();
  char error[96]{};
  const size_t index = a.value < 0 ? SIZE_MAX : static_cast<size_t>(a.value);
  switch (a.kind) {
    case sw::ActionKind::save_memory: {
      sw::Memory memory{};
      memory.frequency_hz = st.frequency_hz;
      memory.bandwidth_hz = g_mode >= 2 ? 2400 : g_filter_hz;
      memory.saved_utc = network_clock::status().wallclock_valid ? static_cast<uint64_t>(time(nullptr)) : 0;
      std::snprintf(memory.mode, sizeof(memory.mode), "%s", mode_text());
      std::snprintf(memory.station, sizeof(memory.station), "%s", sw::pending_memory_label());
      std::snprintf(memory.notes, sizeof(memory.notes), "%s", sw::pending_memory_notes());
      const auto result = lib.memories.upsert(memory);
      if (result == sw::RecordResult::ok && persist_memories(error, sizeof(error))) {
        lib.status = sw::StorageStatus::ready;
        ESP_LOGI(kTag, "memory saved frequency_hz=%lu", static_cast<unsigned long>(memory.frequency_hz));
      } else if (result != sw::RecordResult::duplicate) {
        if (result == sw::RecordResult::ok) lib.memories.discard_last();
        lib.status = sw::StorageStatus::write_failed;
        ESP_LOGW(kTag, "memory save_failed result=%u error=%s", unsigned(result), error);
      }
      break;
    }
    case sw::ActionKind::update_memory:
    case sw::ActionKind::favorite_memory:
    case sw::ActionKind::delete_memory: {
      sw::Memory before{};
      const sw::Memory* existing = lib.memories.at(index);
      const bool had_record = existing != nullptr;
      if (existing) before = *existing;
      auto result = sw::RecordResult::missing;
      if (a.kind == sw::ActionKind::update_memory) {
        if (existing) {
          sw::Memory edited = *existing;
          std::snprintf(edited.station, sizeof(edited.station), "%s", sw::pending_memory_label());
          std::snprintf(edited.notes, sizeof(edited.notes), "%s", sw::pending_memory_notes());
          result = lib.memories.replace(index, edited);
        }
      } else if (a.kind == sw::ActionKind::favorite_memory) {
        result = lib.memories.toggle_favorite(index);
      } else {
        result = lib.memories.erase(index);
      }
      if (result == sw::RecordResult::ok && persist_memories(error, sizeof(error))) {
        lib.status = sw::StorageStatus::ready;
        ESP_LOGI(kTag, "memory changed action=%u index=%u", unsigned(a.kind), unsigned(index));
      } else {
        if (result == sw::RecordResult::ok && had_record) {
          if (a.kind == sw::ActionKind::delete_memory) lib.memories.insert(index, before);
          else lib.memories.replace(index, before);
        }
        lib.status = sw::StorageStatus::write_failed;
        ESP_LOGW(kTag, "memory change_failed action=%u index=%u error=%s", unsigned(a.kind), unsigned(index), error);
      }
      break;
    }
    case sw::ActionKind::save_log: {
      if (!network_clock::status().wallclock_valid) {
        ESP_LOGW(kTag, "log save_failed error=utc_clock_not_set");
        break;
      }
      sw::LogEntry entry{};
      entry.timestamp_utc = static_cast<uint64_t>(time(nullptr));
      entry.frequency_hz = st.frequency_hz;
      entry.bandwidth_hz = g_mode >= 2 ? 2400 : g_filter_hz;
      entry.signal_dbfs = orcsdr::radio::level_dbfs();
      std::snprintf(entry.mode, sizeof(entry.mode), "%s", mode_text());
      std::snprintf(entry.device, sizeof(entry.device), "RTL-SDR");
      std::snprintf(entry.antenna, sizeof(entry.antenna), "%s",
                    sw::pending_log_antenna()[0] ? sw::pending_log_antenna() : "Not recorded");
      std::snprintf(entry.notes, sizeof(entry.notes), "%s", sw::pending_log_notes());
      for (size_t i = 0; i < lib.memories.size(); ++i) {
        const sw::Memory* memory = lib.memories.at(i);
        if (memory && memory->frequency_hz == entry.frequency_hz) {
          std::snprintf(entry.station, sizeof(entry.station), "%s", memory->station);
          break;
        }
      }
      const auto result = lib.logs.append(entry);
      if (result == sw::RecordResult::ok && persist_logs(error, sizeof(error))) {
        lib.status = sw::StorageStatus::ready;
        ESP_LOGI(kTag, "log saved frequency_hz=%lu", static_cast<unsigned long>(entry.frequency_hz));
      } else {
        if (result == sw::RecordResult::ok) lib.logs.discard_last();
        lib.status = sw::StorageStatus::write_failed;
        ESP_LOGW(kTag, "log save_failed result=%u error=%s", unsigned(result), error);
      }
      break;
    }
    case sw::ActionKind::update_log:
    case sw::ActionKind::delete_log: {
      sw::LogEntry before{};
      const sw::LogEntry* existing = lib.logs.at(index);
      const bool had_record = existing != nullptr;
      if (existing) before = *existing;
      auto result = sw::RecordResult::missing;
      if (a.kind == sw::ActionKind::update_log) {
        if (existing) {
          sw::LogEntry edited = *existing;
          std::snprintf(edited.antenna, sizeof(edited.antenna), "%s", sw::pending_log_antenna());
          std::snprintf(edited.notes, sizeof(edited.notes), "%s", sw::pending_log_notes());
          result = lib.logs.replace(index, edited);
        }
      } else {
        result = lib.logs.erase(index);
      }
      if (result == sw::RecordResult::ok && persist_logs(error, sizeof(error))) {
        lib.status = sw::StorageStatus::ready;
        ESP_LOGI(kTag, "log changed action=%u index=%u", unsigned(a.kind), unsigned(index));
      } else {
        if (result == sw::RecordResult::ok && had_record) {
          if (a.kind == sw::ActionKind::delete_log) lib.logs.insert(index, before);
          else lib.logs.replace(index, before);
        }
        lib.status = sw::StorageStatus::write_failed;
        ESP_LOGW(kTag, "log change_failed action=%u index=%u error=%s", unsigned(a.kind), unsigned(index), error);
      }
      break;
    }
    case sw::ActionKind::export_log:
      if (orcsdr::storage::mounted() && sw::export_logs(orcsdr::storage::filesystem(), lib.logs, error, sizeof(error)))
        ESP_LOGI(kTag, "export ok logs=%u formats=csv,adif", unsigned(lib.logs.size()));
      else
        ESP_LOGW(kTag, "export failed error=%s", error);
      break;
    default: break;
  }
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
    case sw::ActionKind::save_memory:
    case sw::ActionKind::update_memory:
    case sw::ActionKind::favorite_memory:
    case sw::ActionKind::delete_memory:
    case sw::ActionKind::save_log:
    case sw::ActionKind::update_log:
    case sw::ActionKind::delete_log:
    case sw::ActionKind::export_log: library_action(a); break;
    default: break;  // hunt and audio clean-up: not available on this target yet
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
bool shortwave_resize_viewport() {
  const auto* geometry=present_geometry();
  if(geometry==nullptr)return sw::set_viewport({0,0,1280,720});
  const auto safe=geometry->safe;
  return sw::set_viewport({static_cast<int>(safe.x),static_cast<int>(safe.y),
                           static_cast<int>(safe.width),static_cast<int>(safe.height)});
}

const Screen& shortwave_screen() {
  static const Screen s{orcsdr::dashboards::Id::shortwave, "SHORTWAVE", enter, leave, tick, tap, drag, spectrum,
      nullptr, []() { if(!shortwave_resize_viewport()) return false; sw::draw(); return true; }};
  return s;
}
}  // namespace screens

#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::shortwave {

enum class ReceiverRoute : uint8_t { unknown, direct_q, hf_upconverter, tuner };
enum class FilterPreset : uint8_t { narrow, normal, wide };
enum class SpectrumRegion : uint8_t { vlf_edge, lf, mf, hf, outside };
enum class Tab : uint8_t { live, on_air, hunt, memory, logbook };
enum class RecordResult : uint8_t { ok, full, duplicate, invalid, missing };

struct ModeGuide {
  const char* likely_mode;
  const char* reason;
  bool supported_now;
};

struct BroadcastBand {
  const char* label;
  uint32_t min_hz;
  uint32_t max_hz;
  uint32_t default_hz;
};

struct StationCard {
  char station[64]{};
  char program[64]{};
  char callsign[16]{};
  char country[40]{};
  char language[32]{};
  char transmitter[48]{};
  char target[48]{};
  char source[32]{};
  uint32_t frequency_hz = 0;
  uint32_t tolerance_hz = 0;
  uint32_t start_utc_minute = 0;
  uint32_t end_utc_minute = 0;
  uint32_t days_mask = 0;
  uint32_t source_version = 0;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint32_t power_w = 0;
};

struct Memory {
  uint32_t frequency_hz = 0;
  uint32_t bandwidth_hz = 0;
  uint64_t saved_utc = 0;
  bool favorite = false;
  char mode[8]{};
  char station[64]{};
  char callsign[16]{};
  char country[40]{};
  char language[32]{};
  char notes[160]{};
};

struct LogEntry {
  uint64_t timestamp_utc = 0;
  int16_t local_offset_minutes = 0;
  uint32_t frequency_hz = 0;
  uint32_t bandwidth_hz = 0;
  float signal_dbfs = 0;
  char mode[8]{};
  char station[64]{};
  char program[64]{};
  char callsign[16]{};
  char country[40]{};
  char language[32]{};
  char device[40]{};
  char antenna[64]{};
  char notes[240]{};
  char recording_path[128]{};
};

class MemoryTable {
 public:
  static constexpr size_t kCapacity = 128;
  RecordResult upsert(const Memory& memory);
  RecordResult insert(size_t index, const Memory& memory);
  RecordResult replace(size_t index, const Memory& memory);
  RecordResult toggle_favorite(size_t index);
  RecordResult erase(size_t index);
  void clear() { size_ = 0; }
  void discard_last() { if (size_) --size_; }
  size_t size() const { return size_; }
  const Memory* at(size_t index) const;

 private:
  Memory records_[kCapacity]{};
  size_t size_ = 0;
};

class LogTable {
 public:
  static constexpr size_t kCapacity = 128;
  RecordResult append(const LogEntry& entry);
  RecordResult insert(size_t index, const LogEntry& entry);
  RecordResult replace(size_t index, const LogEntry& entry);
  RecordResult erase(size_t index);
  void clear() { size_ = 0; }
  void discard_last() { if (size_) --size_; }
  size_t size() const { return size_; }
  const LogEntry* at(size_t index) const;

 private:
  LogEntry records_[kCapacity]{};
  size_t size_ = 0;
};

size_t band_count();
const BroadcastBand* band(size_t index);
size_t station_count();
const StationCard* station_at(size_t index);
const BroadcastBand* band_for(uint32_t frequency_hz);
uint32_t adjacent_band_frequency(uint32_t frequency_hz, int direction);
uint32_t next_tuning_step(uint32_t current_hz);
uint32_t filter_bandwidth(FilterPreset preset);
SpectrumRegion region_for(uint32_t frequency_hz);
const char* region_label(SpectrumRegion region);
ModeGuide mode_guide_for(uint32_t frequency_hz);
bool schedule_matches(const StationCard& card, uint32_t frequency_hz,
                      uint16_t utc_minute, uint8_t utc_weekday);
bool valid(const Memory& memory);
bool valid(const LogEntry& entry);
bool model_self_check();

}  // namespace orcsdr::shortwave

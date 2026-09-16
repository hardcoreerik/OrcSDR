#include "shortwave_model.hpp"

#include <cmath>
#include <cstring>

namespace orcsdr::shortwave {
namespace {

constexpr BroadcastBand kBands[] = {
    {"120m", 2300000, 2498000, 2400000},
    {"90m", 3200000, 3400000, 3300000},
    {"75m", 3900000, 4000000, 3950000},
    {"60m", 4750000, 5060000, 4900000},
    {"49m", 5900000, 6200000, 6000000},
    {"41m", 7200000, 7450000, 7300000},
    {"31m", 9400000, 9900000, 9550000},
    {"25m", 11600000, 12100000, 11780000},
    {"22m", 13570000, 13870000, 13600000},
    {"19m", 15100000, 15800000, 15400000},
    {"16m", 17480000, 17900000, 17600000},
    {"15m", 18900000, 19020000, 18950000},
    {"13m", 21450000, 21850000, 21600000},
    {"11m", 25670000, 26100000, 25800000},
};

constexpr uint32_t kSteps[] = {100, 500, 1000, 5000};

template <size_t Size>
bool terminated(const char (&value)[Size]) {
  return std::memchr(value, '\0', Size) != nullptr;
}

bool text_valid(const Memory& memory) {
  return terminated(memory.mode) && terminated(memory.station) &&
         terminated(memory.callsign) && terminated(memory.country) &&
         terminated(memory.language) && terminated(memory.notes);
}

bool text_valid(const LogEntry& entry) {
  return terminated(entry.mode) && terminated(entry.station) &&
         terminated(entry.program) && terminated(entry.callsign) &&
         terminated(entry.country) && terminated(entry.language) &&
         terminated(entry.device) && terminated(entry.antenna) &&
         terminated(entry.notes) && terminated(entry.recording_path);
}

bool receiver_frequency(uint32_t frequency_hz) {
  return frequency_hz >= 1710000 && frequency_hz <= 30000000;
}

bool receiver_bandwidth(uint32_t bandwidth_hz) {
  return bandwidth_hz >= 3000 && bandwidth_hz <= 30000;
}

}  // namespace

size_t band_count() { return sizeof(kBands) / sizeof(kBands[0]); }

const BroadcastBand* band(size_t index) {
  return index < band_count() ? &kBands[index] : nullptr;
}

const BroadcastBand* band_for(uint32_t frequency_hz) {
  for (const auto& candidate : kBands)
    if (frequency_hz >= candidate.min_hz && frequency_hz <= candidate.max_hz)
      return &candidate;
  return nullptr;
}

uint32_t adjacent_band_frequency(uint32_t frequency_hz, int direction) {
  if (direction == 0) return frequency_hz;
  for (size_t index = 0; index < band_count(); ++index) {
    if (frequency_hz >= kBands[index].min_hz && frequency_hz <= kBands[index].max_hz) {
      const size_t next = direction > 0 ? (index + 1) % band_count()
                                        : (index + band_count() - 1) % band_count();
      return kBands[next].default_hz;
    }
  }
  if (direction > 0) {
    for (const auto& candidate : kBands)
      if (candidate.min_hz > frequency_hz) return candidate.default_hz;
    return kBands[0].default_hz;
  }
  for (size_t index = band_count(); index > 0; --index)
    if (kBands[index - 1].max_hz < frequency_hz) return kBands[index - 1].default_hz;
  return kBands[band_count() - 1].default_hz;
}

uint32_t next_tuning_step(uint32_t current_hz) {
  for (size_t index = 0; index < sizeof(kSteps) / sizeof(kSteps[0]); ++index)
    if (kSteps[index] == current_hz)
      return kSteps[(index + 1) % (sizeof(kSteps) / sizeof(kSteps[0]))];
  return kSteps[0];
}

uint32_t filter_bandwidth(FilterPreset preset) {
  switch (preset) {
    case FilterPreset::narrow: return 4000;
    case FilterPreset::normal: return 6000;
    case FilterPreset::wide: return 9000;
  }
  return 6000;
}

bool valid(const Memory& memory) {
  return receiver_frequency(memory.frequency_hz) &&
         receiver_bandwidth(memory.bandwidth_hz) &&
         strcmp(memory.mode, "AM") == 0 && text_valid(memory);
}

bool valid(const LogEntry& entry) {
  return entry.timestamp_utc != 0 && receiver_frequency(entry.frequency_hz) &&
         receiver_bandwidth(entry.bandwidth_hz) && strcmp(entry.mode, "AM") == 0 &&
         entry.local_offset_minutes >= -14 * 60 &&
         entry.local_offset_minutes <= 14 * 60 && std::isfinite(entry.signal_dbfs) &&
         text_valid(entry);
}

bool model_self_check() {
  const auto* first = band_for(2300000);
  const auto* first_end = band_for(2498000);
  const auto* band49 = band_for(5935000);
  const auto* band11 = band_for(26100000);
  if (band_count() != 14 || !first || strcmp(first->label, "120m") != 0 ||
      first_end != first || band_for(2498001) != nullptr || !band49 ||
      strcmp(band49->label, "49m") != 0 || !band11 ||
      strcmp(band11->label, "11m") != 0 || band_for(26100001) != nullptr)
    return false;
  if (adjacent_band_frequency(6000000, 1) != 7300000 ||
      adjacent_band_frequency(6000000, -1) != 4900000 ||
      adjacent_band_frequency(5500000, 1) != 6000000 ||
      adjacent_band_frequency(5500000, -1) != 4900000)
    return false;
  if (next_tuning_step(100) != 500 || next_tuning_step(500) != 1000 ||
      next_tuning_step(1000) != 5000 || next_tuning_step(5000) != 100 ||
      next_tuning_step(250) != 100 ||
      filter_bandwidth(FilterPreset::narrow) != 4000 ||
      filter_bandwidth(FilterPreset::normal) != 6000 ||
      filter_bandwidth(FilterPreset::wide) != 9000)
    return false;

  Memory memory{};
  memory.frequency_hz = 6010000;
  memory.bandwidth_hz = 6000;
  strcpy(memory.mode, "AM");
  if (!valid(memory)) return false;  // Unidentified stations are valid memories.
  memory.frequency_hz = 1600000;
  if (valid(memory)) return false;
  memory.frequency_hz = 6010000;
  memset(memory.notes, 'x', sizeof(memory.notes));
  if (valid(memory)) return false;

  LogEntry entry{};
  entry.timestamp_utc = 1789440000;
  entry.local_offset_minutes = -7 * 60;
  entry.frequency_hz = 5935000;
  entry.bandwidth_hz = 6000;
  entry.signal_dbfs = -42.5f;
  strcpy(entry.mode, "AM");
  strcpy(entry.notes, "Unidentified voice");
  if (!valid(entry)) return false;  // Station and callsign remain optional.
  entry.local_offset_minutes = 15 * 60;
  return !valid(entry);
}

}  // namespace orcsdr::shortwave

#include "wefax_schedule.hpp"
#include <cstdio>
namespace orcsdr::wefax_schedule {
namespace {
// Station-assigned (not USB dial) values, per NOAA published HF schedules.
constexpr Frequency kNmc[] = {
  {4346000, Daypart::night}, {8682000, Daypart::both},
  {12786000, Daypart::both}, {17151200, Daypart::both}, {22527000, Daypart::day}
};
constexpr Frequency kNmf[] = {
  {4235000, Daypart::night}, {6340500, Daypart::both},
  {9110000, Daypart::both}, {12750000, Daypart::both}
};
constexpr Station kStations[] = {
  {"NMC", "POINT REYES", "Pacific", "https://ocean.weather.gov/shtml/pacsch.php", kNmc, 5},
  {"NMF", "BOSTON", "Atlantic", "https://ocean.weather.gov/shtml/atlsch.php", kNmf, 4}
};
// Deliberately a CURATED subset of published daily starts, NOT a complete schedule.
// Test patterns and representative weather products only. The source pages
// carry many more slots and can change without a firmware update.
constexpr Broadcast kPrograms[] = {
  {0, 1*60+40, "TEST PATTERN", "PZZZ93"},
  {0, 1*60+43, "PACIFIC SATELLITE", "EVPN00"},
  {0, 3*60+5, "SURFACE ANALYSIS 00Z P1", "PYBA01"},
  {0, 6*60+55, "TEST PATTERN", "PZZZ93"},
  {0, 9*60+19, "SURFACE ANALYSIS 06Z P1", "PYBA03"},
  {0, 11*60+20, "TEST PATTERN", "PZZZ93"},
  {0, 14*60, "TEST PATTERN", "PZZZ93"},
  {0, 14*60+3, "PACIFIC SATELLITE", "EVPN13"},
  {1, 2*60+30, "TEST PATTERN", "PZZZ94"},
  {1, 2*60+33, "SURFACE ANALYSIS 00Z", "PYAA10"},
  {1, 4*60+38, "N. AMERICAN ICE CHART", "PIEA88"},
  {1, 7*60+45, "TEST PATTERN", "PZZZ94"},
  {1, 14*60, "TEST PATTERN", "PZZZ94"},
  {1, 16*60, "N. AMERICAN ICE CHART", "PIEA88"},
  {1, 17*60+20, "TEST PATTERN", "PZZZ94"}
};
} // namespace
size_t station_count() { return sizeof(kStations)/sizeof(kStations[0]); }
const Station* station(size_t index) { return index < station_count() ? &kStations[index] : nullptr; }
size_t broadcast_count() { return sizeof(kPrograms)/sizeof(kPrograms[0]); }
const Broadcast* broadcast(size_t index) { return index < broadcast_count() ? &kPrograms[index] : nullptr; }
uint32_t usb_dial_hz(uint32_t assigned_hz) { return assigned_hz >= 1900 ? assigned_hz - 1900 : 0; }
const Frequency* frequency(size_t station_index, size_t frequency_index) {
  const Station* s = station(station_index);
  return s && frequency_index < s->frequency_count ? &s->frequencies[frequency_index] : nullptr;
}
size_t next_broadcasts(uint64_t epoch, int filter, Upcoming* out, size_t cap) {
  if (!epoch || !out || !cap || filter < -1 || (filter >= 0 && static_cast<size_t>(filter) >= station_count())) return 0;
  constexpr uint64_t day = 86400;
  const uint64_t midnight = (epoch / day) * day;
  size_t found = 0;
  // Small sorted insertion array: no heap, sorting, allocation or STL needed.
  for (const auto& b : kPrograms) {
    if (filter >= 0 && b.station_index != filter) continue;
    uint64_t at = midnight + uint64_t(b.utc_minute) * 60;
    if (at <= epoch) at += day;
    size_t pos = 0;
    while (pos < found && out[pos].start_utc <= at) ++pos;
    if (pos >= cap) continue;
    if (found < cap) ++found;
    for (size_t j = found - 1; j > pos; --j) out[j] = out[j-1];
    out[pos] = {&b,at};
  }
  return found;
}
void format_utc_minute(uint16_t minute, char* output, size_t capacity) {
  if (!output || capacity == 0) return;
  if (minute >= 1440) { output[0] = 0; return; }
  std::snprintf(output, capacity, "%02u:%02uZ", unsigned(minute/60), unsigned(minute%60));
}
} // namespace orcsdr::wefax_schedule

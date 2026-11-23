#pragma once
// Offline WEFAX station and selected-program guide. No radio, display or clock dependencies.
// Source: NOAA OPC Point Reyes and Boston schedule pages; see docs/wefax/CLAUDE_HANDOFF.md.
#include <cstddef>
#include <cstdint>
namespace orcsdr::wefax_schedule {
enum class Daypart : uint8_t { day, night, both };
struct Frequency { uint32_t assigned_hz; Daypart published; };
struct Station {
  const char* callsign;
  const char* name;
  const char* region;
  const char* source_url;
  const Frequency* frequencies;
  size_t frequency_count;
};
struct Broadcast {
  uint8_t station_index;
  uint16_t utc_minute;   // 0..1439; daily schedule, UTC, not reception proof
  const char* title;
  const char* product_id;
};
struct Upcoming {
  const Broadcast* broadcast = nullptr;
  uint64_t start_utc = 0;
};
size_t station_count();
const Station* station(size_t index);
size_t broadcast_count();
const Broadcast* broadcast(size_t index);
// Default HF USB dial frequency. The assigned frequency is NOT the receiver dial.
// Arithmetic only, does not reconfigure the SDR or assert tunability.
uint32_t usb_dial_hz(uint32_t assigned_hz);
// Most people want a reference station/frequency, not a false signal forecast.
const Frequency* frequency(size_t station_index, size_t frequency_index);
// Return up to capacity next scheduled starts for a station, or all when filter=-1.
// Unix UTC seconds, caller must first have a VERIFIED clock; epoch 0 returns no entries.
// Strictly future: if the scheduled minute equals now exactly, show next day.
size_t next_broadcasts(uint64_t utc_epoch, int filter_station, Upcoming* output, size_t capacity);
// Pure time formatter, without depending on a local time zone or a clock provider.
void format_utc_minute(uint16_t utc_minute, char* output, size_t capacity);
}  // namespace orcsdr::wefax_schedule

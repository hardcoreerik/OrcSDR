#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr { class NvsStore; }

namespace orcsdr::time_service {

struct Snapshot {
  uint32_t uptime_ms = 0;
  uint32_t utc = 0;
  bool wallclock_valid = false;
};

void initialize(bool previously_established);
Snapshot now();
bool set_utc(uint32_t epoch);
// Puts the system clock back in step with the RTC (after a network sync that could not be written to it).
void resync_system_from_rtc();
bool format_utc(char* output, size_t output_size, uint32_t epoch);

// The hardware RTC always holds UTC. The zone is a UTC offset in minutes east of UTC (UTC-07:00 is -420) that
// turns it into the local time people type and read; it is applied by our own code, never through the TZ
// environment variable, and survives reboots. Daylight saving is a manual offset change.
int32_t utc_offset_minutes();
bool set_utc_offset_minutes(int32_t minutes);        // clamped to UTC-12:00..UTC+14:00; in memory only
void load_config(NvsStore& store);                   // call once after initialize()
bool save_config(NvsStore& store);
// "YYYY-MM-DD HH:MM:SS" at the configured offset (output_size >= 20).
bool format_local(char* output, size_t output_size, uint32_t epoch);
bool self_check();

}  // namespace orcsdr::time_service

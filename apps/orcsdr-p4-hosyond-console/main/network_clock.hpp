#pragma once

#include <cstdint>

#include "clock_sync_status.hpp"

namespace network_clock {

enum class Error : uint8_t { none, no_wifi, busy, timeout, invalid_time, set_time };

struct Status {
  bool wallclock_valid = false;
  orcsdr::clock_sync::Level level = orcsdr::clock_sync::Level::unsynced;
  uint32_t freshness_age_s = 0;
  uint32_t last_success_utc = 0;
  bool syncing = false;
  bool failed = false;
  Error last_error = Error::none;
  uint32_t attempts = 0;
  uint32_t time_step_revision = 0;  // increments for every completed network clock step
  // GPS time (boards with a GNSS receiver). Network time is preferred whenever it is fresh.
  bool gps_time_enabled = true;
  uint32_t last_gps_utc = 0;        // system UTC after the last GPS correction; 0 if none
  uint32_t gps_corrections = 0;
  uint8_t gps_decision = 0;         // last orcsdr_gnss_time_decision_t (0 disabled .. 7 applied)
};

// Call regularly from the application service loop. Never waits for network I/O.
void service(bool wifi_connected);
// Queue a user-requested synchronization. The request is consumed by service().
bool request_sync();
// Queue a supported UTC epoch for application by service() on the owner cadence.
bool request_set_utc(uint32_t epoch);
// Invalidate network freshness after a manual or GNSS clock correction; applied on the next service() call.
void clock_changed();
// Setting for correcting the clock from GNSS time. Persisted; defaults to on. The call that changes it returns
// false if the value could not be saved (the in-memory setting is then unchanged).
bool gps_time_enabled();
bool set_gps_time_enabled(bool enabled);
Status status();

}  // namespace network_clock

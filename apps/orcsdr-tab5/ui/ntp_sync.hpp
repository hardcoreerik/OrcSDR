#pragma once

#include <cstdint>

// Optional network time. Nothing here runs unless the person asks (Settings > System > SET CLOCK > SYNC NTP) and
// Wi-Fi is already connected; the clock never depends on Internet access. A successful sync writes UTC to the
// hardware RTC through time_service::set_utc().
namespace orcsdr::ntp_sync {

enum class State : uint8_t { idle, syncing, done, failed };

// Starts one sync attempt. False if one is already running.
bool start();
// Call from the UI loop; finishes the attempt (or times it out after 20 s).
void poll();
State state();
// True once per finished attempt; `ok` says whether the RTC was set.
bool take_result(bool* ok);

}  // namespace orcsdr::ntp_sync

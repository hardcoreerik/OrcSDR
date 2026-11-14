#pragma once

// Power guard for a bus-powered PlutoSDR. The Pluto runs from the host's USB rail; when a battery-powered host (the Tab5) is discharging and the
// battery runs out, the rail collapses and the Pluto dies without a word and cannot be restarted (seen in an overnight soak). The guard stops the
// stream while there is still battery, says why, and lets the stream resume once the host is charging again or has recovered.
//
// Pure decision logic, no hardware access: pluto_source feeds it the host's battery percent and charge state every few seconds. Unknown
// readings never trip it (a host without a battery, or a failed read, must not block the receiver), and a tripped guard stays tripped until it
// sees a known reading that clears it.

#include <cstdint>

namespace orcsdr::pluto_power_guard {

constexpr int kWarnPct = 20;     // discharging at or below this: one warning
constexpr int kTripPct = 10;     // discharging at or below this: stop the stream
constexpr int kRecoverPct = 25;  // a tripped guard clears when charging, or when the level is back at or above this (hysteresis)

enum class Event : uint8_t { none, warn, trip, recover };

struct State {
  bool tripped = false;
  bool warned = false;
};

// battery_pct: 0..100 or -1 unknown. charging: 1 charging, 0 discharging, -1 unknown.
inline Event update(State& s, int battery_pct, int charging) {
  if (battery_pct < 0 || charging < 0) return Event::none;
  if (s.tripped) {
    if (charging == 1 || battery_pct >= kRecoverPct) {
      s.tripped = false;
      s.warned = false;
      return Event::recover;
    }
    return Event::none;
  }
  if (charging == 0 && battery_pct <= kTripPct) {
    s.tripped = true;
    return Event::trip;
  }
  if (charging == 0 && battery_pct <= kWarnPct) {
    if (!s.warned) {
      s.warned = true;
      return Event::warn;
    }
    return Event::none;
  }
  s.warned = false;  // charging, or comfortably above the warning level: the next dip warns again
  return Event::none;
}

}  // namespace orcsdr::pluto_power_guard

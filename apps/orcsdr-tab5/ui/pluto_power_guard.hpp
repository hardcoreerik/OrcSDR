#pragma once

// Low-battery WARNING for a bus-powered PlutoSDR. The Pluto runs from the host's USB rail; when a battery-powered host (the Tab5) is discharging
// and the battery runs out, the rail collapses and the Pluto dies without a word (seen in an overnight soak). This only tells the user, so they
// can plug in; it never stops, refuses or delays anything. Conditions change during development (power supplies come and go, batteries are
// swapped while the unit is on external power), so no battery reading may ever switch a function off.
//
// It never acts on one reading: a level counts only after it is sustained across several readings in a row, and one good reading resets the count.
// A reading that cannot be trusted (no cell, mid-swap, a failed read) clears the warning rather than keeping it alive: a warning about a battery
// that may no longer be there is worse than none.
//
// Pure decision logic, no hardware access: pluto_source feeds it the host's battery percent and charge state about once a minute.

#include <cstdint>

namespace orcsdr::pluto_power_guard {

constexpr int kLowPct = 20;            // discharging at or below this ...
constexpr uint8_t kLowReadings = 2;    // ... for this many readings in a row: level "low"
constexpr int kCriticalPct = 10;       // discharging at or below this ...
constexpr uint8_t kCriticalReadings = 3;  // ... for this many readings in a row: level "critical"
constexpr int kRecoverPct = 25;        // a low or critical level clears when charging, or when the level is back at or above this (hysteresis)

enum class Level : uint8_t { ok, low, critical };

struct State {
  Level level = Level::ok;
  uint8_t low_run = 0;       // consecutive readings at or below kLowPct while discharging
  uint8_t critical_run = 0;  // consecutive readings at or below kCriticalPct while discharging
};

// battery_pct: 0..100 or -1 unknown. charging: 1 charging, 0 discharging, -1 unknown. Returns true when the level changed.
inline bool update(State& s, int battery_pct, int charging) {
  if (battery_pct < 0 || charging < 0) {  // not trustworthy: forget everything, warn about nothing
    const bool changed = s.level != Level::ok;
    s = State{};
    return changed;
  }
  const bool discharging = charging == 0;
  const bool low_now = discharging && battery_pct <= kLowPct;
  const bool critical_now = discharging && battery_pct <= kCriticalPct;
  s.low_run = low_now ? static_cast<uint8_t>(s.low_run < 255 ? s.low_run + 1 : 255) : 0;
  s.critical_run = critical_now ? static_cast<uint8_t>(s.critical_run < 255 ? s.critical_run + 1 : 255) : 0;

  Level next = s.level;
  if (s.level != Level::ok && (charging == 1 || battery_pct >= kRecoverPct)) {
    next = Level::ok;  // plugged in, or comfortably recovered
  } else if (s.critical_run >= kCriticalReadings) {
    next = Level::critical;
  } else if (s.level == Level::ok && s.low_run >= kLowReadings) {
    next = Level::low;
  }
  const bool changed = next != s.level;
  s.level = next;
  return changed;
}

}  // namespace orcsdr::pluto_power_guard

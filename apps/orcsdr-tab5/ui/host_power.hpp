#pragma once

// The host's own power numbers (battery and USB rail), read from the M5 power manager. Used for logging and for the Pluto low-battery awareness
// (pluto_power_guard.hpp). Unknown values are -1.

namespace orcsdr::host_power {

struct Reading {
  int battery_pct = -1;  // 0..100, -1 unknown
  int battery_mv = -1;
  int vbus_mv = -1;      // reads -1 on the Tab5: that call is not reported there
  int charging = -1;     // 1 charging, 0 discharging, -1 unknown
};

// One read. A unit powered with no battery attached must never look like a flat one: with no live cell (under 2 V, or no value) the battery
// percent and charge state are reported unknown, and unknown readings never count toward anything. Not tested on a battery-less Tab5; the rule does
// not depend on what such a unit reads, only on there being no plausible cell voltage.
Reading read();

}  // namespace orcsdr::host_power

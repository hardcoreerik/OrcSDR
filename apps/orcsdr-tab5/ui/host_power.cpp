#include "host_power.hpp"

#include <M5Unified.h>

namespace orcsdr::host_power {

Reading read() {
  Reading r;
  // No PMIC-type guard: the Tab5 reports an unknown type yet reads level, voltage and charge state fine (the Home power line relies on that).
  r.battery_pct = M5.Power.getBatteryLevel();
  r.battery_mv = M5.Power.getBatteryVoltage();
  r.vbus_mv = M5.Power.getVBUSVoltage();
  switch (M5.Power.isCharging()) {
    case m5::Power_Class::is_charging:
      r.charging = 1;
      break;
    case m5::Power_Class::is_discharging:
      r.charging = 0;
      break;
    default:
      r.charging = -1;
      break;
  }
  if (r.battery_mv < 2000) {  // no live cell: a bench supply or a unit without its battery
    r.battery_pct = -1;
    r.charging = -1;
  }
  return r;
}

}  // namespace orcsdr::host_power

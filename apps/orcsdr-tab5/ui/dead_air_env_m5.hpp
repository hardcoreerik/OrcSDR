#pragma once
// The Tab5's Port A (Grove, SDA GPIO53 / SCL GPIO54) as a dead_air::env::Bus, through M5Unified's Ex_I2C. Firmware only.
#include "dead_air_env.hpp"

namespace orcsdr::dead_air::env {

// Switches the Port A 5 V rail on (an IO-expander pin on the Tab5, off unless something enables it), starts the bus and
// returns it; nullptr when the external I2C bus is not available. Call from the UI task.
Bus* port_a_bus();

}  // namespace orcsdr::dead_air::env

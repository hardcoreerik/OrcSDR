#pragma once
#include "drone_watch_c6_probe.hpp"
namespace orcsdr::drone_watch {
// Must be called on the UI task after any relevant platform subsystem is
// initialized. Calls existing cached Wi-Fi status getters only, no bus RPCs.
C6Capability inspect_c6_without_side_effects();
}

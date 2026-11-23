#pragma once
// Typical radio allocations, so an event can say what usually lives at its frequency ("FM broadcast", "2 m amateur").
// This is context for reading a measurement, not a finding: allocations differ by country, and a level rise on a busy
// band is usually the band's own traffic. Pure data and a lookup, host-tested.
#include <cstdint>

namespace orcsdr::dead_air {

struct Band {
  uint32_t low_hz;
  uint32_t high_hz;
  const char* name;  // at most 30 characters, so it fits beside the event detail buttons
};

// The most specific listed band containing `hz` (inclusive), or nullptr when no common allocation is listed.
const Band* band_for(uint32_t hz);
// "FM broadcast", or "no common allocation listed" for nullptr-free display.
const char* band_name(uint32_t hz);

}  // namespace orcsdr::dead_air

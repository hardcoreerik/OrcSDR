#pragma once

#include "ft8_model.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8 {

// A propagation summary derived only from what the receiver has decoded: how far and in which directions signals are
// arriving. Receive-only; no Internet lookup. Distances and bearings come from 4-character grid-square centres, so they
// are approximate (see distance_bearing()). Nothing here is an SNR or a signal-strength estimate.
constexpr size_t kBearingSectors = 8;  // N, NE, E, SE, S, SW, W, NW

struct Conditions {
  size_t stations = 0;            // unique callsigns with a valid grid
  bool farthest_valid = false;
  float farthest_km = 0.0f;
  float farthest_bearing_deg = 0.0f;
  char farthest_call[16]{};
  float median_km = 0.0f;         // median distance of those stations
  uint16_t sector_counts[kBearingSectors]{};  // unique stations per compass sector, centred on N, NE, ...
};

// Sector index 0-7 for a bearing in degrees (0 = north-centred sector, 1 = north-east, ...). Out-of-range input wraps.
size_t bearing_sector(float bearing_deg);
const char* sector_name(size_t sector);

// Summarises `count` decodes (any order) relative to `station`. A callsign seen several times counts once, using its
// newest decode (the highest utc_epoch). Decodes without a valid grid or callsign are ignored.
bool compute_conditions(const Decode* decodes, size_t count, const GeoPoint& station, Conditions* out);

bool conditions_self_check();

}  // namespace orcsdr::ft8

#pragma once

#include <cmath>
#include <cstddef>

// Scaffold for matching Home lat/lon to a signed catalog regional map pack.
// Expand kHints as new ORCMAP1 packs are published; firmware does not embed
// regional geometry beyond the world-coastlines default.
namespace orcsdr::map_region_packs {

struct Hint {
  const char* id;          // catalog pack id (e.g. lane_county_map)
  const char* title;       // operator-facing label
  float center_lat;
  float center_lon;
  float cover_radius_nm;   // approximate useful coverage for LoRa/ADS-B
};

inline constexpr Hint kHints[] = {
    {"lane_county_map", "Lane County, OR", 44.05f, -123.09f, 60.0f},
};

inline float distance_nm(float lat1, float lon1, float lat2, float lon2) {
  const float mid_lat = (lat1 + lat2) * 0.5f;
  const float dlat_nm = (lat2 - lat1) * 60.0f;
  const float dlon_nm =
      (lon2 - lon1) * 60.0f * std::cos(mid_lat * 0.017453292519943295f);
  return std::sqrt(dlat_nm * dlat_nm + dlon_nm * dlon_nm);
}

// Returns the nearest published regional hint within 2x its cover radius, or
// nullptr when Home is outside every known pack (world coastlines only).
inline const Hint* nearest(float latitude, float longitude) {
  const Hint* best = nullptr;
  float best_nm = 1.0e30f;
  for (const Hint& hint : kHints) {
    const float nm =
        distance_nm(latitude, longitude, hint.center_lat, hint.center_lon);
    if (nm < best_nm) {
      best_nm = nm;
      best = &hint;
    }
  }
  if (best == nullptr) return nullptr;
  if (best_nm > best->cover_radius_nm * 2.0f) return nullptr;
  return best;
}

inline bool self_check() {
  const Hint* eugene = nearest(44.05f, -123.09f);
  const Hint* tokyo = nearest(35.68f, 139.69f);
  return eugene != nullptr && eugene->id[0] == 'l' && tokyo == nullptr &&
         distance_nm(44.0f, -123.0f, 44.0f, -123.0f) < 0.01f;
}

}  // namespace orcsdr::map_region_packs

#include "ft8_conditions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::ft8 {
namespace {

struct Unique {
  char call[16];
  uint32_t utc_epoch;
  float km;
  float bearing;
};

}  // namespace

size_t bearing_sector(float bearing_deg) {
  if (!std::isfinite(bearing_deg)) return 0;
  double b = std::fmod(static_cast<double>(bearing_deg), 360.0);
  if (b < 0.0) b += 360.0;
  return static_cast<size_t>((b + 22.5) / 45.0) % kBearingSectors;
}

const char* sector_name(size_t sector) {
  static const char* const names[kBearingSectors] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return names[sector % kBearingSectors];
}

bool compute_conditions(const Decode* decodes, size_t count, const GeoPoint& station, Conditions* out) {
  if (out == nullptr || (decodes == nullptr && count != 0)) return false;
  Conditions result{};
  Unique unique[kDecodeCapacity];
  size_t n = 0;

  for (size_t i = 0; i < count; ++i) {
    const Decode& d = decodes[i];
    if (d.callsign[0] == '\0' || !maidenhead_valid(d.grid)) continue;
    GeoPoint there{};
    float km = 0.0f, bearing = 0.0f;
    if (!maidenhead_center(d.grid, &there) || !distance_bearing(station, there, &km, &bearing)) continue;

    size_t slot = n;
    for (size_t k = 0; k < n; ++k)
      if (std::strncmp(unique[k].call, d.callsign, sizeof(unique[k].call)) == 0) {
        slot = k;
        break;
      }
    if (slot == n) {
      if (n >= kDecodeCapacity) continue;
      ++n;
    } else if (d.utc_epoch < unique[slot].utc_epoch) {
      continue;  // keep the newest decode of a callsign
    }
    std::snprintf(unique[slot].call, sizeof(unique[slot].call), "%.15s", d.callsign);
    unique[slot].utc_epoch = d.utc_epoch;
    unique[slot].km = km;
    unique[slot].bearing = bearing;
  }

  result.stations = n;
  float distances[kDecodeCapacity];
  for (size_t k = 0; k < n; ++k) {
    distances[k] = unique[k].km;
    ++result.sector_counts[bearing_sector(unique[k].bearing)];
    if (!result.farthest_valid || unique[k].km > result.farthest_km) {
      result.farthest_valid = true;
      result.farthest_km = unique[k].km;
      result.farthest_bearing_deg = unique[k].bearing;
      std::snprintf(result.farthest_call, sizeof(result.farthest_call), "%.15s", unique[k].call);
    }
  }
  if (n > 0) {
    std::sort(distances, distances + n);
    result.median_km = (n & 1u) ? distances[n / 2] : 0.5f * (distances[n / 2 - 1] + distances[n / 2]);
  }
  *out = result;
  return true;
}

bool conditions_self_check() {
  Decode a{}, b{}, c{}, none{};
  std::snprintf(a.callsign, sizeof(a.callsign), "K1AAA");
  std::snprintf(a.grid, sizeof(a.grid), "FN42");
  a.utc_epoch = 100;
  std::snprintf(b.callsign, sizeof(b.callsign), "K1AAA");  // same station, older: ignored
  std::snprintf(b.grid, sizeof(b.grid), "DM03");
  b.utc_epoch = 50;
  std::snprintf(c.callsign, sizeof(c.callsign), "W6BBB");
  std::snprintf(c.grid, sizeof(c.grid), "DM03");
  c.utc_epoch = 120;
  std::snprintf(none.callsign, sizeof(none.callsign), "NOGRID");
  const Decode all[] = {a, b, c, none};
  Conditions r{};
  // Receiver in the Pacific north-west: FN42 (Massachusetts) is east, DM03 (California) is south.
  const GeoPoint station{44.0f, -123.0f, 0};
  return compute_conditions(all, 4, station, &r) && r.stations == 2 && r.farthest_valid &&
         std::strcmp(r.farthest_call, "K1AAA") == 0 && r.farthest_km > 3500.0f && r.farthest_km < 4200.0f &&
         bearing_sector(r.farthest_bearing_deg) == 2 && r.sector_counts[4] == 1 && r.sector_counts[2] == 1 &&
         bearing_sector(0.0f) == 0 && bearing_sector(359.0f) == 0 && bearing_sector(22.4f) == 0 &&
         bearing_sector(22.6f) == 1 && bearing_sector(-90.0f) == 6 && r.median_km > 0.0f;
}

}  // namespace orcsdr::ft8

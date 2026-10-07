#pragma once

#include <cmath>
#include <cstdint>

// The geometry of a dashboard map view (ADS-B radar, LoRa map): a centre, a range in nautical miles, and how many
// pixels that range spans. Pure math, no display or storage, host-tested. Everything that draws on a map (the basemap
// tiles, aircraft, nodes) uses the same scale, so they agree by construction.
namespace orcsdr::map_view {

constexpr double kMetresPerNm = 1852.0;
constexpr double kEquatorMetresPerPixelZ0 = 156543.03392804097;   // Web Mercator, 256 px tiles, at the equator
constexpr double kNmPerDegreeLatitude = 60.0;

// Metres on the ground per screen pixel when `range_nm` spans `radius_px` pixels.
inline double metres_per_pixel(double range_nm, double radius_px) {
  return (range_nm > 0.0 && radius_px > 0.0) ? range_nm * kMetresPerNm / radius_px : 0.0;
}

// The (fractional) Web Mercator zoom, for 256-pixel tiles, at which one pixel is `metres_per_pixel` at `latitude_deg`.
inline double fractional_zoom(double latitude_deg, double metres_per_pixel) {
  if (metres_per_pixel <= 0.0) return 0.0;
  const double lat = latitude_deg < -85.0 ? -85.0 : latitude_deg > 85.0 ? 85.0 : latitude_deg;
  return std::log2(kEquatorMetresPerPixelZ0 * std::cos(lat * M_PI / 180.0) / metres_per_pixel);
}

struct ZoomPlan {
  uint8_t zoom = 0;        // the integer zoom whose tiles are read
  int tile_size_px = 256;  // screen pixels per tile at that zoom; scales the integer zoom to the fractional one
};

// Picks the integer zoom (not above `max_zoom`) and the tile size in pixels that together give exactly the wanted
// scale. Below one tile per 256 px the zoom floor is `min_zoom`. Past `max_zoom` the tiles are simply drawn larger
// (the map engine has no overzoom, so the data is scaled, not invented).
inline ZoomPlan plan_zoom(double fractional, uint8_t min_zoom, uint8_t max_zoom) {
  ZoomPlan plan;
  double base = std::floor(fractional + 1e-9);
  if (base < min_zoom) base = min_zoom;
  if (base > max_zoom) base = max_zoom;
  plan.zoom = static_cast<uint8_t>(base);
  double size = 256.0 * std::pow(2.0, fractional - base);
  if (size < 64.0) size = 64.0;
  if (size > 4096.0) size = 4096.0;
  plan.tile_size_px = static_cast<int>(std::lround(size));
  return plan;
}

// Local east/north offsets in nautical miles from the view centre. East is scaled by cos(latitude), the same local
// scale Web Mercator has, so this agrees with the drawn tiles to well under a pixel over a radar-sized area.
inline void offset_nm(double centre_lat, double centre_lon, double lat, double lon, double* east_nm,
                      double* north_nm) {
  const double scale = std::fmax(0.1, std::cos(centre_lat * M_PI / 180.0));
  *east_nm = (lon - centre_lon) * scale * kNmPerDegreeLatitude;
  *north_nm = (lat - centre_lat) * kNmPerDegreeLatitude;
}

inline bool self_check() {
  // 25 NM across a 180 px radius at 44 N: about 257 m per pixel, which is zoom 8.8.
  const double mpp = metres_per_pixel(25.0, 180.0);
  const double z = fractional_zoom(44.0, mpp);
  if (!(mpp > 257.0 && mpp < 258.5) || !(z > 8.7 && z < 8.9)) return false;
  const ZoomPlan plan = plan_zoom(z, 0, 14);
  if (plan.zoom != 8 || plan.tile_size_px < 256 || plan.tile_size_px >= 512) return false;
  // The tile size reproduces the wanted scale: tile pixels * 2^zoom is the world width in pixels.
  const double world_px = plan.tile_size_px * std::pow(2.0, plan.zoom);
  const double implied_mpp = 2.0 * M_PI * 6378137.0 * std::cos(44.0 * M_PI / 180.0) / world_px;
  if (std::fabs(implied_mpp - mpp) / mpp > 0.01) return false;
  // Capped at the pack's deepest zoom: tiles are drawn larger rather than refused.
  const ZoomPlan capped = plan_zoom(z, 0, 5);
  if (capped.zoom != 5 || capped.tile_size_px < 2048) return false;
  double east = 0, north = 0;
  offset_nm(44.0, -123.0, 45.0, -123.0, &east, &north);
  if (std::fabs(east) > 1e-9 || std::fabs(north - 60.0) > 1e-9) return false;
  offset_nm(60.0, 10.0, 60.0, 11.0, &east, &north);
  return std::fabs(east - 30.0) < 0.05 && std::fabs(north) < 1e-9;   // one degree of longitude at 60 N is 30 NM
}

}  // namespace orcsdr::map_view

#pragma once
#include <algorithm>
#include <cmath>

// Whole-globe FT8/FT4/JS8 projection. The shared map worker renders a centred,
// one-world Web Mercator square; only the basemap pixels are remapped to this
// equirectangular display. The polar caps have NO Mercator source pixels.
namespace orcsdr::ft8::world_map {
constexpr int kMercatorSide = 512;
constexpr double kMercatorLimit = 85.0511287798066;
constexpr double kPi = 3.14159265358979323846;

inline bool project(double latitude, double longitude, int width, int height,
                    int* x, int* y) {
  if (!x || !y || width <= 0 || height <= 0 || !std::isfinite(latitude) ||
      !std::isfinite(longitude) || latitude < -90 || latitude > 90 ||
      longitude < -180 || longitude > 180) return false;
  // Both representations of the antimeridian refer to the same left seam.
  const double lon = longitude == 180 ? -180 : longitude;
  *x = std::clamp(static_cast<int>(std::floor((lon + 180) * width / 360.0)), 0, width - 1);
  *y = std::clamp(static_cast<int>(std::floor((90 - latitude) * height / 180.0)), 0, height - 1);
  return true;
}

// Destination pixel CENTRES become lat/lon first, then use the same spherical
// Mercator y as OrcMaps' LatLonToTileCoord. Source and destination each show
// exactly one complete longitude revolution; never repeat wrapped worlds.
inline bool source_pixel(int dx, int dy, int width, int height, int* sx, int* sy) {
  if (!sx || !sy || width <= 0 || height <= 0 || dx < 0 || dx >= width || dy < 0 || dy >= height)
    return false;
  const double lat = 90.0 - (dy + 0.5) * 180.0 / height;
  if (std::abs(lat) > kMercatorLimit) return false; // actual polar cap: grid only
  const double rad = lat * kPi / 180.0;
  const double mercator_y = (1.0 - std::asinh(std::tan(rad)) / kPi) * 0.5;
  *sx = std::clamp(static_cast<int>(std::floor((dx + 0.5) * kMercatorSide / width)), 0, kMercatorSide - 1);
  *sy = std::clamp(static_cast<int>(std::floor(mercator_y * kMercatorSide)), 0, kMercatorSide - 1);
  return true;
}
}

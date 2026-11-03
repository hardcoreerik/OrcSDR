#pragma once
#include "map_service.hpp"

namespace orcsdr::maps {
// Basemap pixels and radio overlays share the OrcMaps Mercator camera.
struct RegionalView {
  orcmap::Viewport camera{};
  int x = 0, y = 0;
  FrameInfo frame{};
  bool valid = false;
};
RegionalView draw_camera(lgfx::v1::LovyanGFX&, const orcmap::Viewport&, int x, int y,
                         uint16_t background, uint16_t grid, uint16_t ink,
                         const char* preferred = nullptr, bool labels = true, unsigned caption_size = 1);
RegionalView draw_region(lgfx::v1::LovyanGFX&, int32_t latitude_e7, int32_t longitude_e7,
                         double range_nm, int x, int y, int width, int height,
                         uint16_t background, uint16_t grid, uint16_t ink,
                         const char* preferred = nullptr, bool labels = true);
bool project(const RegionalView&, int32_t latitude_e7, int32_t longitude_e7, int* x, int* y);
} // namespace orcsdr::maps

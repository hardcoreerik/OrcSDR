#pragma once

#include <cstddef>
#include <cstdint>

#include "orcsdr_storage.hpp"

namespace lgfx { inline namespace v1 { class LovyanGFX; } }

// The basemap behind the ADS-B radar and the LoRa map, drawn by OrcMaps. The base is the world overview built into the
// firmware (or a deeper world pack from the SD card); a detail pack covering the view is drawn on top of it when one is
// installed. The scale is exact and the same in both directions, so aircraft and nodes projected with project() sit on
// the map where they belong.
namespace orcsdr::offline_map {

struct View {
  float center_lat = 0.0f;
  float center_lon = 0.0f;
  float range_nm = 25.0f;   // spans `radius_px` pixels (half of the smaller side when radius_px is 0)
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  float radius_px = 0.0f;   // pixels that range_nm spans; 0 = min(width, height) / 2
};

// Normal UI-code only. Looks for map packs on the SD card once; never touched by SDR/audio callbacks.
bool load(orcsdr::storage::FileSystem* filesystem);
bool available();
// A short description of what is drawing ("WORLD z5", "WORLD z5 + 1 DETAIL").
const char* source_label();
void draw_base(const View& view, uint16_t water_color, uint16_t road_color,
               uint16_t airport_color, uint16_t border_color);
void draw_base(lgfx::v1::LovyanGFX& display, const View& view, uint16_t water_color,
               uint16_t road_color, uint16_t airport_color, uint16_t border_color);
bool project(const View& view, float latitude, float longitude, int* x, int* y);
bool self_check();

}  // namespace orcsdr::offline_map

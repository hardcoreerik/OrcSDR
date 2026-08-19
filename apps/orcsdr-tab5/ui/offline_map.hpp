#pragma once

#include <FS.h>
#include <cstddef>
#include <cstdint>

namespace orcsdr::offline_map {

constexpr const char kRuntimePath[] = "/orcsdr/data/lane_county_map.idx";

struct View {
  float center_lat = 0.0f;
  float center_lon = 0.0f;
  float range_nm = 25.0f;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// Normal UI-code only. This bounded cache is never touched by SDR/audio callbacks.
bool load(fs::FS* filesystem);
bool available();
void draw_base(const View& view, uint16_t water_color, uint16_t road_color,
               uint16_t airport_color, uint16_t border_color);
bool project(const View& view, float latitude, float longitude, int* x, int* y);
bool self_check();

}  // namespace orcsdr::offline_map

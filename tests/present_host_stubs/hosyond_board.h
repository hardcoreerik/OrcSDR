#pragma once
#include <cstddef>
#include <cstdint>
#include "orcsdr_display_geometry.h"

using esp_err_t = int;
struct hosyond_display_t {
  orcsdr_display_geometry_t geometry{};
  int width = 0, height = 0;
  int bytes_per_pixel = 3;
  size_t stride_bytes = 0, fb_bytes = 0;
  int fb_count = 0;
  uint8_t* fb[2]{};
};
esp_err_t hosyond_board_sync(const hosyond_display_t*, int fb_index, int y0, int y1);
esp_err_t hosyond_board_flip(const hosyond_display_t*, int fb_index);

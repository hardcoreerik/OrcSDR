#include "acars_dirty_rects.hpp"

#include <algorithm>
#include <cstring>

namespace orcsdr::acars_ui {

size_t diff_rects(const uint16_t* previous, const uint16_t* next, int width, int height, DirtyRect* out, size_t capacity,
                  bool* overflow) {
  if (overflow) *overflow = false;
  if (!previous || !next || !out || width <= 0 || height <= 0) {
    if (overflow) *overflow = true;
    return 0;
  }
  size_t count = 0;
  const int tile_cols = (width + kTileW - 1) / kTileW;
  const int tile_rows = (height + kTileH - 1) / kTileH;
  for (int ty = 0; ty < tile_rows; ++ty) {
    const int y0 = ty * kTileH;
    const int th = std::min(kTileH, height - y0);
    int run_start = -1;
    for (int tx = 0; tx <= tile_cols; ++tx) {
      bool dirty = false;
      if (tx < tile_cols) {
        const int x0 = tx * kTileW;
        const size_t bytes = static_cast<size_t>(std::min(kTileW, width - x0)) * sizeof(uint16_t);
        for (int row = y0; row < y0 + th && !dirty; ++row) {
          const size_t offset = static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(x0);
          dirty = std::memcmp(previous + offset, next + offset, bytes) != 0;
        }
      }
      if (dirty && run_start < 0) run_start = tx;
      if (!dirty && run_start >= 0) {
        const int x0 = run_start * kTileW;
        const int x1 = std::min(width, tx * kTileW);
        run_start = -1;
        // Same horizontal span as a rectangle that ends exactly where this tile row starts: grow it downwards.
        bool merged = false;
        for (size_t i = 0; i < count; ++i) {
          if (out[i].x == x0 && out[i].w == x1 - x0 && out[i].y + out[i].h == y0) {
            out[i].h = static_cast<int16_t>(out[i].h + th);
            merged = true;
            break;
          }
        }
        if (merged) continue;
        if (count >= capacity) {
          if (overflow) *overflow = true;
          return count;
        }
        out[count++] = {static_cast<int16_t>(x0), static_cast<int16_t>(y0), static_cast<int16_t>(x1 - x0),
                        static_cast<int16_t>(th)};
      }
    }
  }
  return count;
}

int64_t rect_area(const DirtyRect* rects, size_t count) {
  int64_t total = 0;
  for (size_t i = 0; i < count; ++i) total += static_cast<int64_t>(rects[i].w) * rects[i].h;
  return total;
}

}  // namespace orcsdr::acars_ui

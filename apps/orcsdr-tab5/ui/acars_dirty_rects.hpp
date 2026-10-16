#pragma once
// Frame differencing for the ACARS dashboard. The dashboard composes each frame off-screen; pushing all of it to the
// panel is slow on the Tab5 (the panel is rotated, so a full-frame copy is a strided write), which shows as a visible sweep
// and blocks touch. Only the tiles that differ from the frame already on screen need to be pushed. Pure C++, host-tested.
#include <cstddef>
#include <cstdint>

namespace orcsdr::acars_ui {

struct DirtyRect {
  int16_t x = 0, y = 0, w = 0, h = 0;
};

constexpr int kTileW = 160;
constexpr int kTileH = 34;

// Compares two same-sized RGB565 frames (row stride = width pixels) tile by tile and writes the merged rectangles of
// tiles that differ (horizontal runs, then identical runs stacked vertically). Returns the count written. `overflow` is
// set when more rectangles were needed than `capacity`: the caller must then push the whole frame.
size_t diff_rects(const uint16_t* previous, const uint16_t* next, int width, int height, DirtyRect* out, size_t capacity,
                  bool* overflow);

int64_t rect_area(const DirtyRect* rects, size_t count);

}  // namespace orcsdr::acars_ui

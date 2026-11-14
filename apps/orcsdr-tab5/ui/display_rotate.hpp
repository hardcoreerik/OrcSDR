#pragma once
// Copies a 16-bit picture drawn in the screen's landscape orientation into the Tab5's portrait framebuffer.
//
// The Tab5 panel is 720 x 1280 (portrait) and OrcSDR runs it turned on its side (rotation 1 or 3). M5GFX copies a
// rotated picture one pixel at a time, which scatters writes across the framebuffer. This copies in small square
// tiles instead, so each tile's reads and writes stay in a few cache lines. Same pixels, same placement: to_panel()
// is the LovyanGFX framebuffer mapping, and the host test checks rotate_copy() against it pixel by pixel.
//
// Portable, header only. Used by display_bench.cpp (RTL_DISPLAY_BENCH); nothing on screen uses it yet.
#include <cstddef>
#include <cstdint>
#include <utility>

namespace orcsdr::display_rotate {

struct Geometry {
  int logical_w = 1280;  // what the application draws in (after rotation)
  int logical_h = 720;
  int panel_w = 720;     // framebuffer row length in pixels
  int panel_h = 1280;
  uint8_t rotation = 3;  // 0..7, as M5GFX's setRotation
};

// Logical (x, y) to framebuffer (px, py): Panel_FrameBufferBase's own mapping.
inline void to_panel(const Geometry& g, int x, int y, int* px, int* py) {
  const unsigned r = g.rotation & 7u;
  if ((1u << r) & 0b10010110u) y = g.logical_h - (y + 1);
  if (r & 2u) x = g.logical_w - (x + 1);
  if (r & 1u) std::swap(x, y);
  *px = x;
  *py = y;
}

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

// The framebuffer rectangle covered by the logical rectangle (x0, y0, w, h).
inline Rect panel_rect(const Geometry& g, int x0, int y0, int w, int h) {
  int ax, ay, bx, by;
  to_panel(g, x0, y0, &ax, &ay);
  to_panel(g, x0 + w - 1, y0 + h - 1, &bx, &by);
  Rect r;
  r.x = ax < bx ? ax : bx;
  r.y = ay < by ? ay : by;
  r.w = (ax < bx ? bx - ax : ax - bx) + 1;
  r.h = (ay < by ? by - ay : ay - by) + 1;
  return r;
}

inline uint16_t swap16(uint16_t v) { return static_cast<uint16_t>((v >> 8) | (v << 8)); }

// Copies src (w x h logical pixels, row stride src_stride pixels) to the framebuffer at logical (x0, y0).
// swap_bytes converts M5GFX sprite pixels (byte-swapped RGB565) to the framebuffer's native RGB565.
// The caller keeps the rectangle inside the screen.
inline void rotate_copy(const Geometry& g, const uint16_t* src, int src_stride, int w, int h, int x0, int y0,
                        uint16_t* fb, bool swap_bytes) {
  constexpr int kTile = 32;
  const unsigned r = g.rotation & 7u;
  for (int ty = 0; ty < h; ty += kTile) {
    const int th = h - ty < kTile ? h - ty : kTile;
    for (int tx = 0; tx < w; tx += kTile) {
      const int tw = w - tx < kTile ? w - tx : kTile;
      if (r == 3) {
        // px = y, py = logical_w - 1 - x: one framebuffer row per logical column, written forwards.
        for (int x = tx; x < tx + tw; ++x) {
          uint16_t* out = fb + static_cast<size_t>(g.logical_w - 1 - (x0 + x)) * g.panel_w + (y0 + ty);
          const uint16_t* in = src + static_cast<size_t>(ty) * src_stride + x;
          for (int y = 0; y < th; ++y, in += src_stride) out[y] = swap_bytes ? swap16(*in) : *in;
        }
      } else if (r == 1) {
        // px = logical_h - 1 - y, py = x: one framebuffer row per logical column, written backwards.
        for (int x = tx; x < tx + tw; ++x) {
          uint16_t* out = fb + static_cast<size_t>(x0 + x) * g.panel_w + (g.logical_h - 1 - (y0 + ty));
          const uint16_t* in = src + static_cast<size_t>(ty) * src_stride + x;
          for (int y = 0; y < th; ++y, in += src_stride) *(out - y) = swap_bytes ? swap16(*in) : *in;
        }
      } else {
        for (int y = ty; y < ty + th; ++y)
          for (int x = tx; x < tx + tw; ++x) {
            int px, py;
            to_panel(g, x0 + x, y0 + y, &px, &py);
            const uint16_t v = src[static_cast<size_t>(y) * src_stride + x];
            fb[static_cast<size_t>(py) * g.panel_w + px] = swap_bytes ? swap16(v) : v;
          }
      }
    }
  }
}

}  // namespace orcsdr::display_rotate

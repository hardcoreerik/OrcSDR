#pragma once
// A small M5GFX-style painter over an RGB565 surface. The method names and argument orders follow
// LovyanGFX (drawLine, drawArc, fillTriangle, ...) so icon routines from the Tab5 source can be
// reused almost verbatim. Coordinates are given in the Tab5's design space (1280x720) and are
// mapped onto the surface by (x - origin) * scale, so one routine serves any display size.
// Pure C++ (no LVGL), host-tested.
#include <cstdint>
#include "orcsdr_ui/home_plots.hpp"

namespace orcsdr::ui {

class Painter {
 public:
  // `origin_x/y` is the design-space point drawn at surface (0,0); sx/sy map design pixels to
  // surface pixels (for example 0.625 and 0.6667 for 1280x720 -> 800x480).
  Painter(plots::Surface surface, int origin_x, int origin_y, float sx, float sy)
      : s_(surface), ox_(origin_x), oy_(origin_y), sx_(sx), sy_(sy) {}

  void drawPixel(int x, int y, uint16_t c);
  void drawFastHLine(int x, int y, int w, uint16_t c);
  void drawFastVLine(int x, int y, int h, uint16_t c);
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c);
  void drawRect(int x, int y, int w, int h, uint16_t c);
  void fillRect(int x, int y, int w, int h, uint16_t c);
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c);
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c);
  void drawCircle(int x, int y, int r, uint16_t c);
  void fillCircle(int x, int y, int r, uint16_t c);
  // r0/r1: inner and outer radius in either order. Angles in degrees, 0 = 3 o'clock, increasing
  // clockwise on screen; a range may wrap through 0 (for example 300..60).
  void drawArc(int x, int y, int r0, int r1, float angle0, float angle1, uint16_t c);
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);

 private:
  int X(int x) const;
  int Y(int y) const;
  int W(int w) const;  // a length along x
  int H(int h) const;  // a length along y
  float R(int r) const;
  void ring_pixel(int px, int py, uint16_t c) { plots::set_pixel(s_, px, py, c); }
  plots::Surface s_;
  int ox_, oy_;
  float sx_, sy_;
};

}  // namespace orcsdr::ui

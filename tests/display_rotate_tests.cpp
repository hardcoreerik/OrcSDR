// Host tests for the tiled rotated copy (apps/orcsdr-tab5/ui/display_rotate.hpp): every pixel lands where the
// LovyanGFX framebuffer mapping puts it, with the byte swap, for all eight rotations, odd sizes and both edges.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/display_rotate_tests.cpp
#include "display_rotate.hpp"

#include <cstdio>
#include <vector>

using namespace orcsdr::display_rotate;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// A small portrait panel turned on its side, so every check covers the whole framebuffer.
Geometry geometry(uint8_t rotation) {
  Geometry g;
  g.panel_w = 72;
  g.panel_h = 128;
  const bool landscape = (rotation & 1u) != 0;
  g.logical_w = landscape ? g.panel_h : g.panel_w;
  g.logical_h = landscape ? g.panel_w : g.panel_h;
  g.rotation = rotation;
  return g;
}

void check_copy(uint8_t rotation, int x0, int y0, int w, int h, bool swap) {
  const Geometry g = geometry(rotation);
  std::vector<uint16_t> src(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) src[static_cast<size_t>(y) * w + x] = static_cast<uint16_t>(0x1000 + y * 251 + x * 7);
  std::vector<uint16_t> fb(static_cast<size_t>(g.panel_w) * g.panel_h, 0xBEEF);
  rotate_copy(g, src.data(), w, w, h, x0, y0, fb.data(), swap);
  std::vector<uint16_t> want(fb.size(), 0xBEEF);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int px, py;
      to_panel(g, x0 + x, y0 + y, &px, &py);
      const uint16_t v = src[static_cast<size_t>(y) * w + x];
      want[static_cast<size_t>(py) * g.panel_w + px] = swap ? swap16(v) : v;
    }
  size_t wrong = 0;
  for (size_t i = 0; i < fb.size(); ++i) wrong += fb[i] != want[i];
  if (wrong) std::printf("  rotation=%u x0=%d y0=%d w=%d h=%d swap=%d wrong=%zu\n", rotation, x0, y0, w, h, swap, wrong);
  CHECK(wrong == 0);
  // panel_rect covers exactly the written pixels
  const Rect r = panel_rect(g, x0, y0, w, h);
  size_t outside = 0, inside = 0;
  for (int py = 0; py < g.panel_h; ++py)
    for (int px = 0; px < g.panel_w; ++px) {
      const bool in = px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
      const bool written = want[static_cast<size_t>(py) * g.panel_w + px] != 0xBEEF;
      outside += written && !in;
      inside += in;
    }
  CHECK(outside == 0 && inside == static_cast<size_t>(w) * h);
}

void test_known_mapping() {
  // Rotation 3, the Tab5 default: logical (0,0) is the framebuffer's bottom-left of the first column.
  Geometry g;  // 1280 x 720 logical on a 720 x 1280 panel
  int px, py;
  to_panel(g, 0, 0, &px, &py);
  CHECK(px == 0 && py == 1279);
  to_panel(g, 1279, 719, &px, &py);
  CHECK(px == 719 && py == 0);
  g.rotation = 1;
  to_panel(g, 0, 0, &px, &py);
  CHECK(px == 719 && py == 0);
  // The Home plot (906 x 186 at 330,112; the trace sprite is 904 x 184 at 331,113).
  g.rotation = 3;
  const Rect r = panel_rect(g, 331, 113, 904, 184);
  CHECK(r.x == 113 && r.w == 184 && r.y == 1279 - 331 - 903 && r.h == 904);
}

}  // namespace

int main() {
  test_known_mapping();
  for (uint8_t rotation = 0; rotation < 8; ++rotation) {
    const Geometry g = geometry(rotation);
    for (bool swap : {false, true}) {
      check_copy(rotation, 0, 0, g.logical_w, g.logical_h, swap);  // whole screen
      check_copy(rotation, 5, 3, 33, 31, swap);                     // odd sizes across a tile edge
      check_copy(rotation, g.logical_w - 40, g.logical_h - 17, 40, 17, swap);  // touching the far edges
      check_copy(rotation, 7, 9, 1, 1, swap);
      check_copy(rotation, 1, 2, 64, 1, swap);
      check_copy(rotation, 2, 1, 1, 64, swap);
    }
  }
  if (failures) {
    std::printf("display_rotate_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("display_rotate_tests: ok\n");
  return 0;
}

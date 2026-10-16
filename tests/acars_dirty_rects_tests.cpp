// SPDX-License-Identifier: AGPL-3.0-only
// Host tests for frame differencing: only changed tiles are reported, rectangles cover every changed pixel,
// vertical runs merge, and too many rectangles asks for a whole-frame push.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "acars_dirty_rects.hpp"

using namespace orcsdr::acars_ui;

namespace {
[[noreturn]] void fail(const char* e, int line) { std::fprintf(stderr, "FAIL line=%d check=%s\n", line, e); std::exit(1); }
#define CHECK(x) do { if (!(x)) fail(#x, __LINE__); } while (false)

constexpr int W = 1280, H = 612;

bool covered(const DirtyRect* r, size_t n, int x, int y) {
  for (size_t i = 0; i < n; ++i)
    if (x >= r[i].x && x < r[i].x + r[i].w && y >= r[i].y && y < r[i].y + r[i].h) return true;
  return false;
}
}  // namespace

int main() {
  std::vector<uint16_t> a(W * H, 0), b(W * H, 0);
  DirtyRect r[24];
  bool overflow = true;

  // Identical frames: nothing to push.
  CHECK(diff_rects(a.data(), b.data(), W, H, r, 24, &overflow) == 0 && !overflow);

  // One changed pixel: one tile, and it covers the pixel.
  b[300 * W + 700] = 1;
  size_t n = diff_rects(a.data(), b.data(), W, H, r, 24, &overflow);
  CHECK(n == 1 && !overflow && covered(r, n, 700, 300));
  CHECK(rect_area(r, n) == static_cast<int64_t>(kTileW) * kTileH);

  // Pixels in the last column and the last row (partial edge tiles) are covered too.
  b[(H - 1) * W + (W - 1)] = 2;
  n = diff_rects(a.data(), b.data(), W, H, r, 24, &overflow);
  CHECK(!overflow && covered(r, n, 700, 300) && covered(r, n, W - 1, H - 1));

  // A tall change over several tile rows in one column merges into one rectangle.
  std::fill(b.begin(), b.end(), 0);
  for (int y = 100; y < 300; ++y) b[y * W + 50] = 7;
  n = diff_rects(a.data(), b.data(), W, H, r, 24, &overflow);
  CHECK(n == 1 && !overflow);
  for (int y = 100; y < 300; ++y) CHECK(covered(r, n, 50, y));

  // A wide change over a tile row merges into one horizontal run.
  std::fill(b.begin(), b.end(), 0);
  for (int x = 0; x < W; ++x) b[10 * W + x] = 3;
  n = diff_rects(a.data(), b.data(), W, H, r, 24, &overflow);
  CHECK(n == 1 && r[0].w == W && !overflow);

  // Every changed pixel of a scattered change is covered, and nothing unchanged is pushed except inside touched tiles.
  std::fill(b.begin(), b.end(), 0);
  const int pts[][2] = {{5, 5}, {1200, 40}, {640, 330}, {10, 600}, {1279, 611}, {321, 200}};
  for (auto& p : pts) b[p[1] * W + p[0]] = 9;
  n = diff_rects(a.data(), b.data(), W, H, r, 24, &overflow);
  CHECK(!overflow && n >= 1);
  for (auto& p : pts) CHECK(covered(r, n, p[0], p[1]));
  CHECK(!covered(r, n, 900, 500));

  // More isolated changes than the capacity allows: the caller is told to push the whole frame.
  std::fill(b.begin(), b.end(), 0);
  for (int ty = 0; ty < H / kTileH; ty += 2)
    for (int tx = 0; tx < W / kTileW; tx += 2) b[(ty * kTileH) * W + tx * kTileW] = 5;
  n = diff_rects(a.data(), b.data(), W, H, r, 4, &overflow);
  CHECK(overflow);

  // Bad arguments never read memory and demand a whole push.
  CHECK(diff_rects(nullptr, b.data(), W, H, r, 24, &overflow) == 0 && overflow);

  std::puts("acars_dirty_rects_tests: all passed");
  return 0;
}

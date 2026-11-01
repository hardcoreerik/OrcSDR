#include "../ui/fm_layout.hpp"

#include <cassert>
#include <cstdio>

using orcsdr::audio_header::HeaderRect;

static bool inside(HeaderRect outer, HeaderRect r) {
  return r.w > 0 && r.h > 0 && r.x >= outer.x && r.y >= outer.y &&
      r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h;
}

static void check(HeaderRect v) {
  const auto header = orcsdr::audio_header::make_header_layout(v);
  const auto l = orcsdr::fm::make_layout(v, header.content_top);
  assert(header.valid && l.valid && !l.legacy);
  assert(inside(v, l.content) && inside(v, l.spectrum) && inside(v, l.waterfall));
  assert(l.spectrum.w <= 1188);
  for (int i = 0; i < 5; ++i) {
    assert(inside(v, l.tabs[i]));
    assert(l.tabs[i].w >= 44 && l.tabs[i].h >= 44);
    if (i) assert(l.tabs[i - 1].x + l.tabs[i - 1].w == l.tabs[i].x);
  }
  assert(l.spectrum.y + l.spectrum.h < l.waterfall.y);
  assert(l.waterfall.y + l.waterfall.h <= l.tabs[0].y);
}

int main() {
  const auto l = orcsdr::fm::make_layout({0, 0, 1280, 720}, 112);
  assert(l.valid && l.legacy);
  assert(l.tabs[0].x == 0 && l.tabs[0].y == 630 && l.tabs[0].w == 256 && l.tabs[0].h == 90);
  assert(l.tabs[4].x == 1024 && l.tabs[4].w == 256);
  assert(l.spectrum.x == 46 && l.spectrum.y == 246 && l.spectrum.w == 1188 && l.spectrum.h == 145);
  assert(l.waterfall.x == 46 && l.waterfall.y == 420 && l.waterfall.w == 1188 && l.waterfall.h == 130);
  check({0, 0, 800, 480});
  check({0, 0, 568, 1232});
  check({72, 72, 424, 1088});
  check({72, 72, 1088, 424});
  std::puts("FM shared geometry tests PASS");
}

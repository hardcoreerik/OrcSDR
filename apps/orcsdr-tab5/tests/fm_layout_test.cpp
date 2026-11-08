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

// The three LISTEN summary cards (frequency, programme service, radio text).
static void check_summary(HeaderRect v, bool side_by_side) {
  const auto header = orcsdr::audio_header::make_header_layout(v);
  const auto l = orcsdr::fm::make_layout(v, header.content_top);
  assert(l.valid);
  for (int i = 0; i < 3; ++i) {
    assert(inside(l.content, l.listen_values[i]));
    assert(l.listen_values[i].y + l.listen_values[i].h <= l.listen_values[3].y);  // above the metric cards
  }
  if (side_by_side) {
    for (int i = 0; i < 3; ++i) {
      assert(l.listen_values[i].y == l.listen_values[0].y && l.listen_values[i].h == l.listen_values[0].h);
      assert(l.listen_values[i].h >= 56);  // room for the title strip plus a text line
      if (i) assert(l.listen_values[i - 1].x + l.listen_values[i - 1].w <= l.listen_values[i].x);
    }
  } else {
    for (int i = 0; i < 3; ++i) {
      assert(l.listen_values[i].x == l.listen_values[0].x && l.listen_values[i].w == l.listen_values[0].w);
      if (i) assert(l.listen_values[i].y > l.listen_values[i - 1].y);
    }
  }
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
  check_summary({0, 0, 800, 480}, false);     // Hosyond keeps stacked rows
  check_summary({0, 0, 568, 1232}, false);
  check_summary({72, 72, 424, 1088}, false);  // LilyGO portrait keeps stacked rows
  check_summary({72, 72, 1088, 424}, true);   // LilyGO landscape: side by side
  std::puts("FM shared geometry tests PASS");
}

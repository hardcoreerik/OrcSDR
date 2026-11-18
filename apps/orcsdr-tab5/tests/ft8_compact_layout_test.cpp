#include "../ui/ft8_compact_layout.hpp"

#include <cassert>
#include <cstdio>

using namespace orcsdr::ft8::compact;

static bool overlap(Rect a, Rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static void check(Rect v) {
  const Layout l = make_layout(v);
  assert(l.valid);
  assert(inside(v, l.content) && inside(v, l.tabs));
  for (Rect t : l.tab) assert(inside(l.tabs, t) && t.w >= 44 && t.h >= 44);
  // Every touch target is at least 44 x 44 and inside the content.
  for (Rect r : {l.band_prev, l.dial, l.band_next, l.clock, l.gain, l.newer, l.older, l.clear, l.mode_ft8, l.mode_ft4,
                 l.sync_clock, l.open_clock})
    assert(inside(l.content, r) && r.w >= 44 && r.h >= 44);
  // The LIVE chips, waterfall, timer and list do not overlap each other.
  Rect live[] = {l.band_prev, l.dial, l.band_next, l.clock, l.gain, l.status, l.wf_frame, l.timer, l.live_list};
  for (size_t i = 0; i < sizeof(live) / sizeof(live[0]); ++i) {
    assert(inside(l.content, live[i]));
    for (size_t j = i + 1; j < sizeof(live) / sizeof(live[0]); ++j) assert(!overlap(live[i], live[j]));
  }
  assert(inside(l.wf_frame, l.wf_plot));
  assert(l.live_rows >= 2);
  // DECODES: list rows fit, the paging controls sit below the list and do not overlap it.
  assert(inside(l.content, l.list) && l.list_rows >= 1);
  assert(32 + l.list_rows * l.row_h <= l.list.h);
  for (Rect r : {l.newer, l.older, l.clear, l.page}) assert(!overlap(r, l.list));
  assert(!overlap(l.newer, l.older) && !overlap(l.older, l.page) && !overlap(l.page, l.clear));
  // SETUP: info rows do not overlap each other or the buttons.
  for (int i = 0; i < 6; ++i) {
    assert(inside(l.content, l.info[i]) && l.info[i].h >= 28);
    assert(!overlap(l.info[i], l.mode_ft8) && !overlap(l.info[i], l.sync_clock));
    if (i) assert(!overlap(l.info[i - 1], l.info[i]));
  }
}

int main() {
  assert(!make_layout({0, 0, 1280, 720}).valid);   // the legacy size belongs to ft8_dashboard.cpp
  check({72, 72, 424, 1088});
  check({72, 72, 1088, 424});
  check({72, 72, 416, 1136});
  check({72, 72, 1136, 416});
  check({0, 0, 800, 480});
  std::puts("FT8 compact layout geometry PASS");
}

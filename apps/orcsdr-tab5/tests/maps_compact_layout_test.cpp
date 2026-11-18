#include <algorithm>
#include "../ui/maps_compact_layout.hpp"

#include <cassert>
#include <cstdio>

using namespace orcsdr::maps_ui::compact;

static bool overlap(Rect a, Rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static void check(Rect v) {
  const Layout l = make_layout(v);
  assert(l.valid);
  assert(inside(v, l.content));
  assert(l.rows == (l.portrait ? 6 : 3));
  Rect all[32];
  int n = 0;
  all[n++] = l.summary;
  for (int i = 0; i < l.rows; ++i) all[n++] = l.row[i];
  for (Rect b : {l.prev, l.label, l.next, l.rescan}) all[n++] = b;
  for (int i = 0; i < n; ++i) {
    assert(inside(l.content, all[i]));
    for (int j = i + 1; j < n; ++j) assert(!overlap(all[i], all[j]));
  }
  assert(inside(l.content, l.pick_map) && !overlap(l.pick_map, l.summary) && !overlap(l.pick_map, l.prev) && !overlap(l.pick_map, l.rescan));
  assert(l.pick_map.w >= 300 && l.pick_map.h >= 150 && l.pick_map.w <= 1280 && l.pick_map.h <= 720);
  for (Rect b : {l.prev, l.next, l.rescan}) assert(b.w >= 44 && b.h >= 44);
  for (int i = 0; i < l.rows; ++i) assert(l.row[i].h >= 44);
}

int main() {
  assert(!make_layout({0, 0, 1280, 720}).valid);   // the legacy size belongs to the Tab5 map screen
  check({72, 72, 424, 1088});
  check({72, 72, 1088, 424});
  check({72, 72, 416, 1136});
  check({72, 72, 1136, 416});
  std::puts("MAPS compact layout geometry PASS");
}

#include <algorithm>
#include "../ui/dead_air_compact_layout.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace orcsdr::dead_air::compact;

static bool overlap(Rect a, Rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static void disjoint(const Layout& l, std::vector<Rect> r) {
  for (Rect x : r) assert(inside(l.content, x));
  for (size_t i = 0; i < r.size(); ++i)
    for (size_t j = i + 1; j < r.size(); ++j) assert(!overlap(r[i], r[j]));
}

static void check(Rect v) {
  const Layout l = make_layout(v);
  assert(l.valid);
  assert(inside(v, l.content) && inside(v, l.tabs));
  for (Rect t : l.tab) assert(inside(l.tabs, t) && t.w >= 44 && t.h >= 44);
  disjoint(l, {l.status, l.level, l.history, l.scan_button, l.record_button});
  assert(l.scan_button.h >= 44 && l.record_button.h >= 44 && l.scan_button.w >= 44);
  disjoint(l, {l.plot, l.waterfall, l.tune_down, l.frequency, l.tune_up});
  assert(l.tune_down.w >= 44 && l.tune_up.w >= 44);
  std::vector<Rect> ev;
  const int rows = l.portrait ? kEventRows : 3;
  for (int i = 0; i < rows; ++i) { assert(l.row[i].h >= 40); ev.push_back(l.row[i]); }
  for (int i = rows; i < kEventRows; ++i) assert(l.row[i].w == 0);
  for (Rect b : {l.prev, l.label, l.next}) ev.push_back(b);
  disjoint(l, ev);
  std::vector<Rect> st;
  for (Rect b : l.setup_button) { assert(b.h >= 44); st.push_back(b); }
  disjoint(l, st);
}

int main() {
  assert(!make_layout({0, 0, 1280, 720}).valid);   // the legacy size belongs to dead_air_dashboard.cpp
  check({72, 72, 424, 1088});
  check({72, 72, 1088, 424});
  check({72, 72, 416, 1136});
  check({72, 72, 1136, 416});
  std::puts("Dead Air compact layout geometry PASS");
}

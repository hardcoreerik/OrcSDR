#include "../ui/ais_compact_layout.hpp"

#include <cassert>
#include <cstdio>

using namespace orcsdr::ais_ui::compact;

static bool overlap(Rect a, Rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static void check(Rect v) {
  const Layout l = make_layout(v);
  assert(l.valid);
  assert(inside(v, l.content) && inside(v, l.tabs));
  for (Rect t : l.tab) assert(inside(l.tabs, t) && t.w >= 44 && t.h >= 44);
  // TARGETS: list, detail and paging controls inside the content and not overlapping; rows fit above the paging strip.
  for (Rect r : {l.context, l.target_list, l.target_detail}) assert(inside(l.content, r));
  assert(!overlap(l.target_list, l.target_detail) && !overlap(l.context, l.target_list) && !overlap(l.context, l.target_detail));
  for (Rect r : {l.page_prev, l.page_next}) assert(inside(l.target_list, r) && r.w >= 44 && r.h >= 44);
  assert(!overlap(l.page_prev, l.page_label) && !overlap(l.page_label, l.page_next));
  assert(l.target_rows >= 1 && l.target_rows * l.target_row_h + 4 <= l.target_list.h - 44);
  // MESSAGES.
  assert(inside(l.content, l.message_list));
  for (Rect r : {l.message_prev, l.message_next}) assert(inside(l.content, r) && r.w >= 44 && r.h >= 44);
  assert(30 + l.message_rows * l.message_row_h <= l.message_list.h - 44);
  // SIGNAL.
  for (Rect r : {l.signal_panel[0], l.signal_panel[1], l.signal_status}) assert(inside(l.content, r));
  assert(!overlap(l.signal_panel[0], l.signal_panel[1]) && !overlap(l.signal_panel[0], l.signal_status) &&
         !overlap(l.signal_panel[1], l.signal_status));
  // SETUP.
  for (int i = 0; i < l.setup_rows; ++i) {
    assert(inside(l.content, l.setup_row[i]) && l.setup_row[i].h >= 28);
    if (i) assert(!overlap(l.setup_row[i - 1], l.setup_row[i]));
  }
}

int main() {
  assert(!make_layout({0, 0, 1280, 720}).valid);   // the legacy size belongs to ais_dashboard.cpp
  check({72, 72, 424, 1088});
  check({72, 72, 1088, 424});
  check({72, 72, 416, 1136});
  check({72, 72, 1136, 416});
  check({0, 0, 800, 480});
  std::puts("AIS compact layout geometry PASS");
}

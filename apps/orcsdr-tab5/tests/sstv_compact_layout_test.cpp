#include <algorithm>
#include "../ui/sstv_compact_layout.hpp"

#include <cassert>
#include <cstdio>

using namespace orcsdr::sstv::compact;

static bool overlap(Rect a, Rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static void check(Rect v) {
  const Layout l = make_layout(v);
  assert(l.valid);
  assert(inside(v, l.content) && inside(v, l.tabs));
  for (Rect t : l.tab) assert(inside(l.tabs, t) && t.w >= 44 && t.h >= 44);
  // RECEIVE: info, picture area and buttons are inside the content and do not overlap.
  for (Rect r : {l.info, l.image, l.abort_button, l.save_button}) assert(inside(l.content, r));
  assert(!overlap(l.info, l.image) && !overlap(l.abort_button, l.save_button) && !overlap(l.image, l.abort_button) &&
         !overlap(l.image, l.save_button) && !overlap(l.info, l.abort_button) && !overlap(l.info, l.save_button));
  assert(l.abort_button.w >= 44 && l.abort_button.h >= 44 && l.save_button.w >= 44 && l.save_button.h >= 44);
  // IMAGES: cells and paging buttons inside the content, no overlaps.
  for (int i = 0; i < kGalleryCells; ++i) {
    assert(inside(l.content, l.cell[i]) && l.cell[i].w >= 80 && l.cell[i].h >= 60);
    for (int j = i + 1; j < kGalleryCells; ++j) assert(!overlap(l.cell[i], l.cell[j]));
    for (Rect b : {l.gallery_prev, l.gallery_next, l.gallery_save, l.gallery_clear, l.gallery_label}) assert(!overlap(l.cell[i], b));
  }
  for (Rect b : {l.gallery_prev, l.gallery_next, l.gallery_save, l.gallery_clear}) assert(inside(l.content, b) && b.w >= 44 && b.h >= 44);
  // TUNE: every control inside the content and none overlapping.
  Rect tune[] = {l.profile[0], l.profile[1], l.profile[2], l.profile[3], l.profile[4], l.frequency, l.nudge_down, l.nudge_up,
                 l.step_button, l.demod_button, l.gain_auto, l.gain_down, l.gain_up, l.if_button, l.level};
  const size_t n = sizeof(tune) / sizeof(tune[0]);
  for (size_t i = 0; i < n; ++i) {
    assert(inside(l.content, tune[i]));
    for (size_t j = i + 1; j < n; ++j) assert(!overlap(tune[i], tune[j]));
  }
  for (size_t i = 0; i < n - 1; ++i) if (i != 5) assert(tune[i].h >= 44);   // the frequency read-out is not a button
  // SETUP.
  Rect setup[] = {l.detection_button, l.mode_button, l.autosave_button, l.clear_button, l.setup_note};
  for (size_t i = 0; i < 5; ++i) {
    assert(inside(l.content, setup[i]));
    for (size_t j = i + 1; j < 5; ++j) assert(!overlap(setup[i], setup[j]));
  }
}

int main() {
  assert(!make_layout({0, 0, 1280, 720}).valid);   // the legacy size belongs to sstv_dashboard.cpp
  check({72, 72, 424, 1088});
  check({72, 72, 1088, 424});
  check({72, 72, 416, 1136});
  check({72, 72, 1136, 416});
  std::puts("SSTV compact layout geometry PASS");
}

#include "../ui/settings_layout.hpp"

#include <cassert>
#include <cstdio>

using namespace orcsdr::settings;

static void inside(Rect outer, Rect inner) {
  assert(orcsdr::audio_header::header_inside(outer, inner));
}

static void check_system(Rect content, bool shallow) {
  if (!shallow) {
    const auto l = make_system_layout(content);
    inside(content, l.title);
    inside(content, l.set_clock);
    inside(content, l.run_setup);
    inside(content, l.note);
    assert(l.set_clock.h >= 44 && l.run_setup.h >= 44);
    for (const auto& row : l.power) inside(content, row.bounds);
    for (const auto& row : l.status) inside(content, row.bounds);
    inside(content, l.power_bounds);
    return;
  }
  const auto status = make_system_layout(content, 0);
  const auto power = make_system_layout(content, 1);
  inside(content, status.title);
  inside(content, status.set_clock);
  inside(content, status.run_setup);
  assert(status.set_clock.h >= 44 && status.run_setup.h >= 44);
  for (const auto& row : status.status) inside(content, row.bounds);
  inside(content, power.title);
  inside(content, power.set_clock);
  inside(content, power.run_setup);
  inside(content, power.power_bounds);
  inside(content, power.note);
  for (const auto& row : power.power) inside(content, row.bounds);
  for (const auto& row : power.status) assert(row.bounds.w == 0);
}

static bool overlaps(Rect a, Rect b) {
  return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 && a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

// The optional GPS TIME toggle: absent unless asked for (so other profiles keep the exact old geometry), inside
// the content when present, and clear of every other control and row.
static void check_gps_toggle(Rect content, bool shallow) {
  const std::size_t pages = shallow ? 2 : 1;
  for (std::size_t page = 0; page < pages; ++page) {
    const auto off = make_system_layout(content, page);
    const auto explicit_off = make_system_layout(content, page, false);
    assert(!off.has_gps_time && off.gps_time.w == 0 && off.gps_time.h == 0);
    assert(off.note.y == explicit_off.note.y && off.note.h == explicit_off.note.h);
    const auto on = make_system_layout(content, page, true);
    if (shallow && page == 1) {   // the second shallow page carries only power rows and the note
      assert(!on.has_gps_time);
      continue;
    }
    assert(on.has_gps_time);
    inside(content, on.gps_time);
    assert(on.gps_time.h >= 26);
    for (const Rect r : {on.title, on.set_clock, on.run_setup, on.power_bounds, on.note})
      assert(!overlaps(on.gps_time, r));
    for (const auto& row : on.status) assert(!overlaps(on.gps_time, row.bounds));
    for (const auto& row : on.power) assert(!overlaps(on.gps_time, row.bounds));
    if (on.note.w > 0) inside(content, on.note);   // the shallow first page has no note
    // Everything else is where it was without the toggle, except the tall note that moves down one row.
    assert(on.set_clock.y == off.set_clock.y && on.run_setup.y == off.run_setup.y);
    for (int i = 0; i < 7; ++i) assert(on.status[i].bounds.y == off.status[i].bounds.y);
    // The tall note is the 71-character "System UTC; Wi-Fi sets time after boot..." sentence: three 24 px lines at
    // this width, so with the toggle it gets 72 px (48 px cut the last words off on the LilyGO). Without the toggle
    // the note keeps its original 48 px.
    if (!shallow) assert(on.note.y == off.note.y + 52 && on.gps_time.h >= 44 && on.note.h >= 72 && off.note.h == 48);
    else assert(on.note.y == off.note.y);
  }
}

static void check_clock(Rect content, Rect footer) {
  const auto l = make_clock_editor_layout(content, footer);
  inside(l.bounds, l.title);
  for (int i = 0; i < 5; ++i) {
    inside(l.bounds, l.field_label[i]);
    inside(l.bounds, l.field_value[i]);
    inside(l.bounds, l.field_plus[i]);
    inside(l.bounds, l.field_minus[i]);
    assert(l.field_plus[i].w >= 44 && l.field_plus[i].h >= 44);
    assert(l.field_minus[i].w >= 44 && l.field_minus[i].h >= 44);
  }
  for (const Rect r : {l.offset_label, l.offset_value, l.offset_minus, l.offset_plus,
                       l.preview, l.note, l.ntp, l.cancel, l.save}) inside(l.bounds, r);
  for (const Rect r : {l.offset_minus, l.offset_plus, l.ntp, l.cancel, l.save})
    assert(r.w >= 44 && r.h >= 44);
  assert(l.bounds.y + l.bounds.h <= footer.y + footer.h);
}

int main() {
  const auto legacy = make_clock_editor_layout({286, 100, 994, 620}, {286, 672, 994, 48}, true);
  assert(legacy.legacy);
  assert(legacy.field_plus[0].x == 360 && legacy.field_plus[0].y == 178);
  assert(legacy.field_minus[4].x == 1048 && legacy.field_minus[4].y == 290);
  assert(legacy.offset_minus.x == 760 && legacy.offset_plus.x == 1100);
  assert(legacy.ntp.x == 360 && legacy.cancel.x == 640 && legacy.save.x == 920);

  const auto portrait = make_settings_layout({72, 72, 424, 1088});
  const auto landscape = make_settings_layout({72, 72, 1088, 424});
  assert(portrait.valid && landscape.valid);
  check_system(portrait.content, false);
  check_system(landscape.content, true);
  check_system({72, 164, 1088, 248}, true);
  check_gps_toggle(portrait.content, false);
  check_gps_toggle(landscape.content, true);
  check_gps_toggle({72, 164, 1088, 248}, true);
  check_clock(portrait.content, portrait.footer);
  check_clock(landscape.content, landscape.footer);
  check_clock({72, 164, 1088, 208}, {72, 376, 1088, 48});
  check_clock({72, 164, 1088, 208}, {72, 372, 1088, 48});
  std::puts("Responsive System and clock geometry PASS");
}

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
  check_clock(portrait.content, portrait.footer);
  check_clock(landscape.content, landscape.footer);
  check_clock({72, 164, 1088, 208}, {72, 376, 1088, 48});
  check_clock({72, 164, 1088, 208}, {72, 372, 1088, 48});
  std::puts("Responsive System and clock geometry PASS");
}

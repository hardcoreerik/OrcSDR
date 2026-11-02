#include "../ui/settings_layout.hpp"

#include <cassert>
#include <cstdio>

using namespace orcsdr::settings;

static bool overlaps(Rect a, Rect b) {
  return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 &&
         a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

static void check_radio(Rect content) {
  for (const bool safe_mode : {false, true}) {
    const auto layout = make_radio_defaults_layout(content, safe_mode);
    assert(orcsdr::audio_header::header_inside(content, layout.title));
    if (safe_mode) assert(orcsdr::audio_header::header_inside(content, layout.note));
    for (const auto& row : layout.rows) {
      assert(orcsdr::audio_header::header_inside(content, row.bounds));
      assert(orcsdr::audio_header::header_inside(content, row.label));
      if (row.value.w) assert(orcsdr::audio_header::header_inside(content, row.value));
      if (row.action.w) {
        assert(row.action.w >= 44 && row.action.h >= 44);
        assert(orcsdr::audio_header::header_inside(content, row.action));
        assert(!overlaps(row.label, row.action));
        assert(!overlaps(row.value, row.action));
      }
    }
    for (std::size_t i = 0; i < 6; ++i)
      for (std::size_t j = i + 1; j < 6; ++j)
        assert(!overlaps(layout.rows[i].bounds, layout.rows[j].bounds));
  }
}

static void check_companion(Rect content, bool shallow) {
  const auto web = make_companion_layout(content, false, !shallow);
  const Rect web_rects[] = {web.title, web.web_status, web.web_action, web.url,
                            web.discovery, web.note, web.bluetooth};
  for (const auto rect : web_rects)
    assert(orcsdr::audio_header::header_inside(content, rect));
  assert(web.web_action.w >= 44 && web.web_action.h >= 44);
  assert(!overlaps(web.web_status, web.web_action));
  for (std::size_t i = 0; i < std::size(web_rects); ++i)
    for (std::size_t j = i + 1; j < std::size(web_rects); ++j)
      assert(!overlaps(web_rects[i], web_rects[j]));

  if (shallow) {
    const auto dial = make_companion_layout(content, true, true);
    const Rect dial_rects[] = {dial.orcdial_title, dial.orcdial_status,
                               dial.orcdial_prompt, dial.primary_action,
                               dial.secondary_action, dial.boot_action,
                               dial.orcdial_error};
    for (const auto rect : dial_rects)
      assert(orcsdr::audio_header::header_inside(content, rect));
    for (const auto action : {dial.primary_action, dial.secondary_action,
                              dial.boot_action})
      assert(action.w >= 44 && action.h >= 44);
    for (std::size_t i = 0; i < std::size(dial_rects); ++i)
      for (std::size_t j = i + 1; j < std::size(dial_rects); ++j)
        assert(!overlaps(dial_rects[i], dial_rects[j]));
  } else {
    const auto all = make_companion_layout(content, false, true);
    const Rect dial_rects[] = {all.orcdial_title, all.orcdial_status,
                               all.orcdial_prompt, all.primary_action,
                               all.secondary_action, all.boot_action,
                               all.orcdial_error};
    for (const auto rect : dial_rects)
      assert(orcsdr::audio_header::header_inside(content, rect));
    for (std::size_t i = 0; i < std::size(dial_rects); ++i)
      for (std::size_t j = i + 1; j < std::size(dial_rects); ++j)
        assert(!overlaps(dial_rects[i], dial_rects[j]));
  }
}

int main() {
  const auto legacy = make_settings_layout({0, 0, 1280, 720});
  assert(legacy.valid && legacy.legacy);
  assert(legacy.row(0).minus.x == 930 && legacy.row(0).minus.y == 144);
  assert(legacy.row(4).action.y == 544);

  const auto portrait = make_settings_layout({72, 72, 424, 1088});
  const auto landscape = make_settings_layout({72, 72, 1088, 424});
  assert(portrait.valid && landscape.valid);
  assert(portrait.content.w == 424 && portrait.content.h >= 800);
  assert(landscape.content.w == 1088 && landscape.content.h >= 200);
  check_radio(portrait.content);
  check_radio(landscape.content);
  check_companion(portrait.content, false);
  check_companion(landscape.content, true);
  check_radio({72, 164, 1088, 248});
  check_companion({72, 164, 1088, 248}, true);
  std::puts("Responsive fixed Settings sections PASS");
}

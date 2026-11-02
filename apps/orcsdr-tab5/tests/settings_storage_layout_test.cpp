#include "../ui/settings_storage_layout.hpp"

#include <cassert>
#include <cstdio>

using namespace orcsdr::settings;

static bool overlaps(Rect a, Rect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

int main() {
  const Rect viewports[] = {{72, 72, 424, 1088}, {72, 72, 1088, 424},
                            {0, 0, 800, 480}};
  for (const Rect viewport : viewports) {
    const auto settings = make_settings_layout(viewport);
    assert(settings.valid && !settings.legacy);
    const auto storage = make_storage_layout(settings.content);
    assert(orcsdr::audio_header::header_inside(settings.content, storage.title));
    assert(orcsdr::audio_header::header_inside(settings.content, storage.note));
    assert(storage.note.h >= storage_note_line_count(storage.note.w) * 24);

    for (const auto& item : storage.items) {
      assert(orcsdr::audio_header::header_inside(settings.content, item.label));
      assert(orcsdr::audio_header::header_inside(settings.content, item.value));
      assert(item.label.h >= 24 && item.value.h >= 24);
      assert(item.value.y >= item.label.y + item.label.h);
      assert(!overlaps(item.label, item.value));
      assert(!overlaps(storage.note, item.label));
      assert(!overlaps(storage.note, item.value));
    }
  }

  const auto narrow = make_settings_layout({72, 72, 424, 1088});
  const auto narrow_storage = make_storage_layout(narrow.content);
  assert(!narrow_storage.two_columns);
  const auto landscape = make_settings_layout({72, 72, 1088, 424});
  const auto landscape_storage = make_storage_layout(landscape.content);
  assert(landscape_storage.two_columns);
  const auto hosyond = make_settings_layout({0, 0, 800, 480});
  const auto hosyond_storage = make_storage_layout(hosyond.content);
  assert(hosyond_storage.two_columns);

  std::puts("Settings storage text geometry PASS");
}

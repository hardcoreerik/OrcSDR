#include "M5Unified.h"
#include "text_editor.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iterator>

RecordingM5 M5;

using namespace orcsdr::text_editor;

static bool same(RecordedRect a, Rect b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static bool overlaps(Rect a, Rect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

static void check_layout(Rect viewport) {
  const Layout l = make_layout(viewport, true);
  assert(l.valid && !l.legacy);
  assert(inside(viewport, l.title) && inside(viewport, l.field));
  assert(inside(viewport, l.keyboard) && inside(viewport, l.accept));
  assert(!overlaps(l.title, l.field) && !overlaps(l.field, l.keyboard));
  assert(!overlaps(l.keyboard, l.shift) && !overlaps(l.shift, l.accept));
  for (int r = 0; r < 4; ++r) {
    for (std::size_t c = 0; c < l.key_counts[r]; ++c) {
      const Rect key = l.keys[r][c];
      assert(inside(viewport, key));
      assert(key.w >= 44 && key.h >= 44);
      assert(!overlaps(key, l.field) && !overlaps(key, l.shift));
      for (int other_r = 0; other_r <= r; ++other_r) {
        const std::size_t limit = other_r == r ? c : l.key_counts[other_r];
        for (std::size_t other_c = 0; other_c < limit; ++other_c)
          assert(!overlaps(key, l.keys[other_r][other_c]));
      }
    }
  }
  const Rect controls[] = {l.shift, l.symbols, l.space, l.back,
                           l.reveal, l.cancel, l.accept};
  for (const Rect control : controls) {
    assert(inside(viewport, control));
    assert(control.w >= 44 && control.h >= 44);
  }
  for (std::size_t i = 0; i < std::size(controls); ++i)
    for (std::size_t j = i + 1; j < std::size(controls); ++j)
      assert(!overlaps(controls[i], controls[j]));
}

static void tap(Rect r) {
  const Result result = handle_touch(r.x + r.w / 2, r.y + r.h / 2);
  if (result != Result::changed) {
    std::fprintf(stderr, "tap missed rect %d,%d %dx%d value=%s active=%d\n",
                 r.x, r.y, r.w, r.h, value(), active() ? 1 : 0);
    assert(result == Result::changed);
  }
}

static void exercise_glyphs(Rect viewport, bool shifted, bool symbols) {
  static constexpr const char* normal[] = {
      "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
  static constexpr const char* upper[] = {
      "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  static constexpr const char* symbol[] = {
      "!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>?/`~", ""};
  const char* const* expected = symbols ? symbol : shifted ? upper : normal;
  begin("TEST", "", 63, false, "SAVE");
  assert(set_viewport(viewport));
  draw();
  if (symbols) {
    const Rect toggle = layout().symbols;
    tap(toggle);
  }
  for (int r = 0; r < 4; ++r) {
    for (std::size_t c = 0; c < std::strlen(expected[r]); ++c) {
      if (shifted) tap(layout().shift);
      tap(layout().keys[r][c]);
      const std::size_t length = std::strlen(value());
      assert(length && value()[length - 1] == expected[r][c]);
    }
  }
}

static void check_legacy_and_bounded_edits() {
  M5.Display = RecordingDisplay{};
  begin("EDIT", "", 8, false, "SAVE");
  assert(layout().legacy);
  assert(set_viewport({0, 72, 1280, 648}));
  const Layout legacy = layout();
  assert(legacy.field.x == 20 && legacy.field.y == 124 &&
         legacy.field.w == 1240 && legacy.field.h == 64);
  assert(legacy.keys[0][0].x == 24 && legacy.keys[0][0].y == 202 &&
         legacy.keys[0][0].w == 116 && legacy.keys[0][0].h == 74);
  assert(legacy.keys[3][0].x == 210 && legacy.keys[3][0].y == 448);
  draw();
  assert(!M5.Display.full_clears);
  assert(same(M5.Display.fills.front(), legacy.viewport));

  const int clears = M5.Display.full_clears;
  const std::size_t before = M5.Display.fills.size();
  tap(legacy.keys[1][0]);
  assert(std::strcmp(value(), "q") == 0 && M5.Display.last_text == "q");
  assert(M5.Display.full_clears == clears && M5.Display.fills.size() == before + 1);
  assert(same(M5.Display.fills.back(), legacy.field));
  tap(legacy.space);
  assert(std::strcmp(value(), "q ") == 0);
  assert(same(M5.Display.fills.back(), legacy.field));
  tap(legacy.back);
  assert(std::strcmp(value(), "q") == 0);
  assert(same(M5.Display.fills.back(), legacy.field));
  assert(M5.Display.full_clears == clears);
  const std::size_t keyboard_before = M5.Display.fills.size();
  tap(legacy.shift);
  assert(M5.Display.full_clears == clears);
  assert(same(M5.Display.fills[keyboard_before], legacy.keyboard));
  for (std::size_t i = keyboard_before; i < M5.Display.fills.size(); ++i)
    assert(!same(M5.Display.fills[i], legacy.viewport) &&
           !same(M5.Display.fills[i], legacy.field));
}

static void check_mask_resize_hardware_and_actions() {
  M5.Display = RecordingDisplay{};
  begin("PASSWORD", "pw", 8, true, "CONNECT");
  assert(set_viewport({72, 72, 424, 960}));
  draw();
  const uint64_t owner = generation();
  const Layout portrait = layout();
  assert(portrait.valid && !portrait.legacy && masked());
  const int clears = M5.Display.full_clears;
  const std::size_t touch_before = M5.Display.fills.size();
  tap(portrait.keys[1][0]);
  assert(std::strcmp(value(), "pwq") == 0 && M5.Display.last_text == "***");
  assert(M5.Display.fills.size() == touch_before + 1);
  assert(same(M5.Display.fills.back(), portrait.field));

  const orcsdr::keyboard_input::Key typed{orcsdr::keyboard_input::Special::none, 'r', false, false};
  assert(handle_key(typed) == orcsdr::keyboard_input::EditAction::edited);
  assert(std::strcmp(value(), "pwqr") == 0 && M5.Display.last_text == "****");
  assert(same(M5.Display.fills.back(), portrait.field));
  assert(M5.Display.full_clears == clears);

  const orcsdr::keyboard_input::Key reveal{orcsdr::keyboard_input::Special::tab, '\0', false, false};
  const std::size_t reveal_before = M5.Display.fills.size();
  assert(handle_key(reveal) == orcsdr::keyboard_input::EditAction::toggle_reveal);
  assert(M5.Display.last_text == "HIDE");
  assert(M5.Display.fills.size() == reveal_before + 2);
  assert(same(M5.Display.fills[reveal_before], portrait.field));
  const auto reveal_touch = layout().reveal;
  tap(reveal_touch);
  assert(M5.Display.last_text == "SHOW");
  assert(M5.Display.full_clears == clears);

  assert(set_viewport({72, 72, 1088, 360}));
  assert(generation() == owner && std::strcmp(value(), "pwqr") == 0 && masked());
  const Layout landscape = layout();
  assert(landscape.valid && !landscape.legacy);
  const std::size_t before_draw = M5.Display.fills.size();
  draw();
  assert(M5.Display.fills.size() > before_draw);
  assert(same(M5.Display.fills[before_draw], landscape.viewport));
  assert(same(M5.Display.fills.back(), landscape.accept));
  int x = 0, y = 0;
  accept_point(&x, &y);
  assert(x == landscape.accept.x + landscape.accept.w / 2 &&
         y == landscape.accept.y + landscape.accept.h / 2);
  assert(handle_touch(x, y) == Result::accepted);
  assert(!active());

  begin("NAME", "draft", 16, false, "SAVE");
  assert(set_viewport({72, 72, 424, 1088}));
  const uint64_t new_owner = generation();
  const char saved[16] = "draft";
  const Layout wide_portrait = layout();
  cancel_point(&x, &y);
  assert(x == wide_portrait.cancel.x + wide_portrait.cancel.w / 2 &&
         y == wide_portrait.cancel.y + wide_portrait.cancel.h / 2);
  assert(generation() == new_owner && std::strcmp(value(), saved) == 0);
  assert(self_check());
  assert(generation() == new_owner && std::strcmp(value(), saved) == 0);
}

static void check_shifted_character_updates_field() {
  M5.Display = RecordingDisplay{};
  begin("NAME", "", 16, false, "SAVE");
  assert(set_viewport({72, 72, 424, 960}));
  draw();
  const Layout l = layout();
  const int clears = M5.Display.full_clears;
  tap(l.shift);
  const std::size_t fills_before = M5.Display.fills.size();
  const std::size_t texts_before = M5.Display.texts.size();
  tap(l.keys[1][0]);
  assert(std::strcmp(value(), "Q") == 0);
  assert(M5.Display.full_clears == clears);

  bool drew_field_value = false;
  for (std::size_t i = texts_before; i < M5.Display.texts.size(); ++i) {
    const auto& call = M5.Display.texts[i];
    if (call.value == "Q" && call.x == l.field.x + l.field.w / 2 &&
        call.y == l.field.y + l.field.h / 2) {
      drew_field_value = true;
      break;
    }
  }
  assert(drew_field_value);

  bool repainted_field = false;
  for (std::size_t i = fills_before; i < M5.Display.fills.size(); ++i) {
    assert(!same(M5.Display.fills[i], l.viewport));
    if (same(M5.Display.fills[i], l.field)) repainted_field = true;
  }
  assert(repainted_field);
}

int main() {
  check_layout({72, 72, 424, 960});
  check_layout({72, 72, 1088, 360});
  check_layout({72, 72, 424, 1088});
  check_layout({72, 72, 1088, 424});
  check_legacy_and_bounded_edits();
  exercise_glyphs({72, 72, 424, 960}, false, false);
  exercise_glyphs({72, 72, 424, 960}, true, false);
  exercise_glyphs({72, 72, 424, 960}, false, true);
  exercise_glyphs({72, 72, 1088, 360}, false, false);
  exercise_glyphs({72, 72, 1088, 360}, true, false);
  exercise_glyphs({72, 72, 1088, 360}, false, true);
  check_mask_resize_hardware_and_actions();
  check_shifted_character_updates_field();
  std::puts("Text editor layout and renderer PASS");
}

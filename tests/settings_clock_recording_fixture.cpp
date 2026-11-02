#include "settings_app.cpp"

#include <cstring>

M5Stub M5;
extern "C" size_t strlcpy(char* destination, const char* source, size_t size) {
  if (!destination || size == 0) return source ? std::strlen(source) : 0;
  const size_t length = source ? std::strlen(source) : 0;
  const size_t copied = std::min(length, size - 1);
  if (copied) std::memcpy(destination, source, copied);
  destination[copied] = '\0';
  return length;
}

namespace orcsdr::focus_nav {
void note(int, int, int, int) {}
void clear() {}
bool set_viewport(int, int) { return true; }
}
namespace orcsdr::audio_header {
const HeaderLayout& layout() { static HeaderLayout value{}; return value; }
bool set_viewport(HeaderRect) { return true; }
void draw_brand(const char*) {}
void draw_battery(int32_t) {}
void draw_mute_button(bool) {}
void draw_visualizer_button(bool) {}
void draw_settings_button() {}
}
#ifndef ORCSDR_TEST_REAL_TEXT_EDITOR
namespace orcsdr::text_editor {
void begin(const char*, const char*, size_t, bool, const char*) {}
bool set_viewport(Rect) { return true; }
const Layout& layout() { static Layout value{}; return value; }
bool active() { return false; }
uint64_t generation() { return 0; }
const char* value() { return ""; }
void draw() {}
Result handle_touch(int, int) { return Result::none; }
keyboard_input::EditAction handle_key(const keyboard_input::Key&) { return {}; }
bool masked() { return false; }
void accept_point(int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
void cancel_point(int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
void close() {}
bool self_check() { return true; }
}

#endif

namespace {
using namespace orcsdr::settings;

bool setup_clock(int year, int month, int day, int offset = 0) {
  M5.Display.clear_records();
  g_layout = make_settings_layout({0, 0, 800, 480});
  if (!g_layout.valid || g_layout.legacy) return false;
  State state{};
  state.rtc_valid = true;
  state.utc_offset_minutes = static_cast<int16_t>(offset);
  orcsdr::clock_settings::LocalTime local{year, month, day, 12, 0, 0};
  if (!orcsdr::clock_settings::local_to_utc(local, offset, &state.rtc_epoch)) return false;
  strlcpy(state.rtc_utc, "2025-01-31 12:00:00", sizeof(state.rtc_utc));
  enter(state, Section::system);
  return begin_clock_edit();
}

ClockEditorLayout clock_layout() {
  return make_clock_editor_layout(g_layout.content, g_layout.footer, false);
}

bool tap(Rect r, Action* action = nullptr) {
  const auto result = handle_touch(r.x + r.w / 2, r.y + r.h / 2);
  if (action) *action = result;
  return true;
}

bool has_text(const char* expected, int x, int y) {
  for (const auto& text : M5.Display.texts)
    if (text.text == expected && text.x == x && text.y == y) return true;
  return false;
}
}

extern "C" __declspec(dllexport) int test_compact_minus_keeps_clock_editor() {
  using namespace orcsdr::settings;
  if (!setup_clock(2025, 1, 31)) return 1;
  const auto layout = clock_layout();
  const Rect minus = layout.field_minus[0];
  if (minus.x + minus.w / 2 >= 286) return 2;
  tap(minus);
  return g_clock_edit && geometry_locked() && g_clock_local.year == 2024 ? 0 : 3;
}

extern "C" __declspec(dllexport) int test_month_clamp_repaints_only_affected_fields() {
  using namespace orcsdr::settings;
  if (!setup_clock(2025, 1, 31)) return 1;
  const auto layout = clock_layout();
  M5.Display.clear_records();
  tap(layout.field_plus[1]);
  if (g_clock_local.month != 2 || g_clock_local.day != 28 ||
      M5.Display.fill_screen_calls != 0 || M5.Display.fills.size() != 3) return 2;
  bool month_fill = false, day_fill = false, preview_fill = false;
  int64_t fill_area = 0;
  for (const auto& fill : M5.Display.fills) {
    fill_area += int64_t(fill.w) * fill.h;
    month_fill |= fill.x == layout.field_value[1].x && fill.y == layout.field_value[1].y &&
                  fill.w == layout.field_value[1].w && fill.h == layout.field_value[1].h;
    day_fill |= fill.x == layout.field_value[2].x && fill.y == layout.field_value[2].y &&
                fill.w == layout.field_value[2].w && fill.h == layout.field_value[2].h;
    preview_fill |= fill.x == layout.preview.x && fill.y == layout.preview.y &&
                    fill.w == layout.preview.w && fill.h == layout.preview.h;
  }
  const auto month = layout.field_value[1], day = layout.field_value[2];
  return month_fill && day_fill && preview_fill && fill_area < 30000 &&
      has_text("02", month.x + month.w / 2, month.y + month.h / 2) &&
      has_text("28", day.x + day.w / 2, day.y + day.h / 2) ? 0 : 3;
}

extern "C" __declspec(dllexport) int test_cancel_discards_clock_draft() {
  using namespace orcsdr::settings;
  if (!setup_clock(2025, 1, 31)) return 1;
  const uint32_t original_utc = g_state.rtc_epoch;
  auto layout = clock_layout();
  tap(layout.field_plus[1]);
  Action action{};
  layout = clock_layout();
  tap(layout.cancel, &action);
  if (action.kind != ActionKind::none || g_clock_edit || g_state.rtc_epoch != original_utc ||
      geometry_locked() || !begin_clock_edit()) return 2;
  return g_clock_local.year == 2025 && g_clock_local.month == 1 && g_clock_local.day == 31 ? 0 : 3;
}

extern "C" __declspec(dllexport) int test_offset_only_save_emits_offset_action() {
  using namespace orcsdr::settings;
  if (!setup_clock(2025, 1, 31)) return 1;
  const uint32_t original_utc = g_state.rtc_epoch;
  const auto layout = clock_layout();
  tap(layout.offset_plus);
  Action action{};
  tap(layout.save, &action);
  return action.kind == ActionKind::clock_offset_changed && action.value == 15 &&
      !g_clock_edit && !g_clock_dirty && g_state.rtc_epoch == original_utc ? 0 : 2;
}

extern "C" __declspec(dllexport) int test_invalid_dirty_clock_cannot_save() {
  using namespace orcsdr::settings;
  if (!setup_clock(2025, 1, 31)) return 1;
  g_clock_local.year = 2100;
  g_clock_dirty = true;
  Action action{};
  tap(clock_layout().save, &action);
  return action.kind == ActionKind::none && g_clock_edit && geometry_locked() ? 0 : 2;
}

extern "C" __declspec(dllexport) int test_titles_stay_below_navigation() {
  using namespace orcsdr::settings;
  for(const Rect view: {Rect{72,72,424,1088},Rect{72,72,1088,424}}) {
    g_layout=make_settings_layout(view);State state{};
    M5.Display.clear_records();enter(state,Section::system);
    const auto system=make_system_layout(g_layout.content);
    bool found=false;
    for(const auto& t:M5.Display.texts) if(t.text=="SYSTEM" && t.x==system.title.x) {
      found=true;if(t.y-12<g_layout.content.y) return 1;
    }
    if(!found) return 2;
    M5.Display.clear_records();begin_clock_edit();
    const auto clock=make_clock_editor_layout(g_layout.content,g_layout.footer);
    found=false;
    for(const auto& t:M5.Display.texts) if(t.text=="SET CLOCK" && t.x==clock.title.x) {
      found=true;if(t.y-12<clock.bounds.y) return 3;
    }
    if(!found) return 4;
    leave();
  }
  return 0;
}

#include "../apps/orcsdr-tab5/ui/p25_dashboard.hpp"
#include "../apps/orcsdr-tab5/ui/p25_dashboard_layout.hpp"
#include "../apps/orcsdr-tab5/ui/focus_nav.hpp"
#include "../apps/orcsdr-tab5/ui/dashboard_audio_control.hpp"
#include "../apps/orcsdr-tab5/ui/text_editor.hpp"
#include "../apps/orcsdr-tab5/ui/waterfall_style.hpp"
#include "p25_ui_stubs/M5Unified.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {
bool g_editor_active = false;
char g_editor_value[48]{};
std::vector<orcsdr::focus_nav::Rect> g_focus;
}

namespace orcsdr::focus_nav {
void note(int x, int y, int w, int h) { g_focus.push_back({x, y, w, h}); }
bool set_viewport(int width, int height) { return width > 0 && height > 0; }
void clear() { g_focus.clear(); }
}

namespace orcsdr::audio_header {
bool set_viewport(HeaderRect) { return true; }
const HeaderLayout& layout() { static HeaderLayout l = make_header_layout({0, 0, 1280, 720}); return l; }
void reset(Control&) {}
void draw_brand(const char*) {}
void draw(Control const&, uint8_t, bool, int32_t) {}
void draw_home_button() {}
void draw_mute_button(bool) {}
void draw_visualizer_button(bool) {}
void draw_settings_button() {}
bool settings_hit(int32_t, int32_t) { return false; }
bool service_timeout(Control&, uint32_t) { return false; }
bool self_check() { return true; }
}

namespace orcsdr::text_editor {
bool active() { return g_editor_active; }
Result handle_touch(int, int) { return Result::none; }
const char* value() { return g_editor_value; }
void begin(const char*, const char* initial, size_t maximum_length, bool, const char*) {
  strlcpy(g_editor_value, initial, std::min(maximum_length + 1, sizeof(g_editor_value)));
  g_editor_active = true;
}
bool set_viewport(Rect area) { return area.w >= 100 && area.h >= 100; }
void draw() {}
}

namespace orcsdr::p25config {
bool self_check() { return true; }
}

namespace orcsdr::p25core {
const char* modulation_name(Modulation) { return "C4FM"; }
}

namespace orcsdr::waterfall_style {
uint8_t default_palette(Screen) { return 0; }
uint8_t palette(Screen) { return 0; }
uint8_t speed(Screen) { return 0; }
void set_palette(Screen, uint8_t) {}
void set_speed(Screen, uint8_t) {}
void next_palette(Screen) {}
void next_speed(Screen) {}
const char* palette_name(uint8_t) { return "EMBER"; }
const char* speed_name(uint8_t) { return "1X"; }
int rows_per_frame(Screen) { return 1; }
void reset_clock(Screen, uint32_t) {}
int rows_due(Screen, uint32_t, uint32_t) { return 1; }
uint16_t color565(Screen, float level) { return static_cast<uint16_t>(level * 65535.0f); }
uint16_t color565(uint8_t, float level) { return static_cast<uint16_t>(level * 65535.0f); }
uint8_t pack(Screen) { return 0; }
void unpack(Screen, uint8_t) {}
void set_persist_hook(PersistHook) {}
}

using orcsdr::p25::ActionKind;
using orcsdr::p25::Snapshot;
using orcsdr::p25::View;
using orcsdr::p25::layout::Layout;
using orcsdr::p25::layout::Rect;

static void tap(const Rect& r) {
  (void)orcsdr::p25::handle_touch(r.x + r.w / 2, r.y + r.h / 2);
}

static Snapshot populated() {
  Snapshot s{};
  strlcpy(s.config.system_name, "LONG P25 SYSTEM NAME FOR SCREEN FIT TESTING", sizeof(s.config.system_name));
  strlcpy(s.config_status, "All profile fields stored", sizeof(s.config_status));
  s.frequency_hz = 851012500;
  s.span_hz = 960000;
  s.running = s.driver_ready = s.wifi_connected = s.sound_enabled = true;
  s.relative_dbfs = -42.5f;
  s.config.control_channel_count = 8;
  for (int i = 0; i < 8; ++i) s.config.control_channels_hz[i] = 851000000u + static_cast<uint32_t>(i) * 12500u;
  s.config.talkgroup_count = 8;
  for (int i = 0; i < 8; ++i) {
    s.config.talkgroups[i].id = static_cast<uint16_t>(1000 + i);
    snprintf(s.config.talkgroups[i].alias, sizeof(s.config.talkgroups[i].alias), "MAXIMUM ALIAS %02d", i);
  }
  s.profiles.count = 16;
  s.profiles.active_index = 7;
  for (int i = 0; i < 16; ++i) {
    snprintf(s.profiles.profiles[i].id, sizeof(s.profiles.profiles[i].id), "profile-%02d", i);
    snprintf(s.profiles.profiles[i].name, sizeof(s.profiles.profiles[i].name), "Stored System Profile %02d", i);
  }
  s.decoded.frame_sync = s.decoded.identity_valid = true;
  s.decoded.nac = 0x293;
  s.decoded.wacn = 0xABCDE;
  s.decoded.system_id = 0x123;
  s.decoded.current_grant.valid = true;
  s.decoded.current_grant.talkgroup = 1000;
  s.decoded.current_grant.frequency_hz = 851012500;
  s.decoded.current_grant.source_id = 123456;
  s.decoded.current_grant.seen_ms = test_millis;
  return s;
}

static void verify_changed_and_idle_update(Snapshot* snapshot) {
  using namespace orcsdr::p25;
  static int sequence = 0;
  char expected[64]{};
  ++sequence;
  if (view() == View::program)
    snprintf(snapshot->profiles.profiles[15].name, sizeof(snapshot->profiles.profiles[15].name),
             "PROFILE UPDATE %02d", sequence);
  else if (view() == View::talkgroups)
    snprintf(snapshot->config.system_name, sizeof(snapshot->config.system_name), "TALK UPDATE %02d", sequence);
  else {
    snapshot->frequency_hz += 12500;
    snprintf(expected, sizeof(expected), "%.4f", snapshot->frequency_hz / 1000000.0);
  }

  const int clears = lgfx::screen_clears;
  const size_t begin = lgfx::paints.size();
  test_millis += 250;
  update(*snapshot);
  assert(lgfx::screen_clears == clears);
  assert(lgfx::paints.size() > begin);
  bool displayed = false;
  if (view() == View::program) snprintf(expected, sizeof(expected), "PROFILE UPDATE %02d", sequence);
  else if (view() == View::talkgroups) snprintf(expected, sizeof(expected), "TALK UPDATE %02d", sequence);
  for (size_t i = begin; i < lgfx::paints.size(); ++i)
    displayed |= lgfx::paints[i].text.find(expected) != std::string::npos;
  assert(displayed);
  const size_t idle = lgfx::paints.size();
  update(*snapshot);
  assert(lgfx::screen_clears == clears && lgfx::paints.size() == idle);
  if (view() == View::program)
    snprintf(snapshot->profiles.profiles[15].name, sizeof(snapshot->profiles.profiles[15].name),
             "Stored System Profile 15");
  else if (view() == View::talkgroups)
    strlcpy(snapshot->config.system_name, "LONG P25 SYSTEM NAME FOR SCREEN FIT TESTING",
            sizeof(snapshot->config.system_name));
  else snapshot->frequency_hz -= 12500;
  test_millis += 250;
  update(*snapshot);
}

int main() {
  using namespace orcsdr::p25;
  Snapshot s = populated();
  enter(s);
  assert(active() && view() == View::monitor);
  int clears = lgfx::screen_clears;
  const size_t idle_paints = lgfx::paints.size();
  update(s);
  update(s);
  assert(lgfx::screen_clears == clears);
  assert(lgfx::paints.size() == idle_paints);

  s.frequency_hz += 12500;
  test_millis += 250;
  const size_t paint_count = lgfx::paints.size();
  const size_t fill_count = lgfx::filled_rects.size();
  update(s);
  assert(lgfx::screen_clears == clears);
  assert(lgfx::paints.size() > paint_count);
  for (size_t i = fill_count; i < lgfx::filled_rects.size(); ++i)
    assert(lgfx::filled_rects[i].w < 1280 || lgfx::filled_rects[i].h < 720);
  const std::pair<Control, ActionKind> controls[] = {
      {Control::previous_candidate, ActionKind::previous_candidate}, {Control::next_candidate, ActionKind::next_candidate},
      {Control::survey_toggle, ActionKind::survey_toggle}, {Control::hold_toggle, ActionKind::hold_toggle},
      {Control::skip_talkgroup, ActionKind::skip_talkgroup}, {Control::span_down, ActionKind::span_down},
      {Control::span_up, ActionKind::span_up}, {Control::auto_follow_toggle, ActionKind::auto_follow_toggle},
      {Control::encryption_skip_toggle, ActionKind::encryption_skip_toggle}, {Control::import_profile, ActionKind::import_profile},
      {Control::reload_config, ActionKind::reload_config}, {Control::sound_toggle, ActionKind::sound_toggle},
      {Control::volume_down, ActionKind::volume_down}, {Control::volume_up, ActionKind::volume_up},
      {Control::open_device_settings, ActionKind::open_device_settings}, {Control::exit_to_home, ActionKind::exit_to_home}};
  for (const auto& item : controls) assert(handle_control(item.first).kind == item.second);
  assert(handle_control(Control::tune_hz, 1).value == 24000000);
  assert(handle_control(Control::tune_hz, 2000000000u).value == 1766000000u);
  assert(handle_control(Control::hold_talkgroup, 1007).value == 1007);
  assert(handle_control(Control::hold_talkgroup, 9999).kind == ActionKind::none);

  for (const auto viewport : {Rect{0, 0, 1280, 720}, Rect{0, 0, 800, 480},
                              Rect{72, 72, 424, 1088}, Rect{72, 72, 1088, 424}}) {
    enter(s);
    const int before_resize = lgfx::screen_clears;
    assert(set_viewport(viewport.x, viewport.y, viewport.w, viewport.h));
    assert(lgfx::screen_clears == before_resize || lgfx::screen_clears == before_resize + 1);
    Layout l = layout::make_layout(viewport);
    g_focus.clear();
    lgfx::paints.clear();
    clears = lgfx::screen_clears;
    draw();
    assert(lgfx::screen_clears == ++clears);
    bool saw_tgid = false, saw_source = false, saw_identity = false, saw_voice_frequency = false;
    for (const auto& paint : lgfx::paints) {
      saw_tgid |= paint.text.find("TGID 1000") != std::string::npos;
      saw_source |= paint.text.find("SOURCE 123456") != std::string::npos;
      saw_identity |= paint.text.find("WACN ABCDE") != std::string::npos;
      saw_voice_frequency |= paint.text.find("VOICE 851.0125 MHz") != std::string::npos;
    }
    assert(saw_tgid && saw_source && saw_identity && saw_voice_frequency);
    for (const auto& r : g_focus) {
      assert(r.x >= viewport.x && r.y >= viewport.y);
      assert(r.x + r.w <= viewport.x + viewport.w && r.y + r.h <= viewport.y + viewport.h);
      assert(r.w >= 44 && r.h >= 44);
    }
    if (viewport.h > 720) {
      bool focus_below_tab5 = false;
      for (const auto& r : g_focus) focus_below_tab5 |= r.y >= 720;
      assert(focus_below_tab5);
    }
    std::vector<float> levels(256, -42.0f);
    const ActionKind monitor_actions[] = {ActionKind::previous_candidate, ActionKind::next_candidate,
        ActionKind::survey_toggle, ActionKind::hold_toggle, ActionKind::skip_talkgroup};
    for (int i = 0; i < 5; ++i) {
      const auto r = l.monitor_actions[i];
      assert(handle_touch(r.x + r.w / 2, r.y + r.h / 2).kind == monitor_actions[i]);
    }
    tap(l.tab[1]);
    assert(view() == View::spectrum);
    verify_changed_and_idle_update(&s);
    assert(handle_touch(l.span_down.x + l.span_down.w / 2,
                        l.span_down.y + l.span_down.h / 2).kind == ActionKind::span_down);
    assert(handle_touch(l.span_up.x + l.span_up.w / 2,
                        l.span_up.y + l.span_up.h / 2).kind == ActionKind::span_up);
    const auto tuned = handle_touch(l.spectrum_plot.x + l.spectrum_plot.w / 2,
                                    l.spectrum_plot.y + l.spectrum_plot.h / 2);
    assert(tuned.kind == ActionKind::tune_hz && tuned.value == s.frequency_hz);
    draw_spectrum(levels.data(), 0, levels.size(), -90.0f);
    const size_t expected_max = static_cast<size_t>(1186) * 143 * 2;
    assert(lgfx::live_sprite_bytes <= expected_max);
    const size_t tg_begin = lgfx::paints.size();
    tap(l.tab[2]);
    assert(view() == View::talkgroups);
    verify_changed_and_idle_update(&s);
    for (int page = 0; page < 8; ++page) tap(l.talk_previous);
    for (size_t page = 0; page < 7 / static_cast<size_t>(l.talk_row_count); ++page)
      tap(l.talk_next);
    const int last_talk_row = 7 - static_cast<int>(7 / l.talk_row_count) * l.talk_row_count;
    const auto held = handle_touch(l.talk_rows[last_talk_row].x + 4,
                                   l.talk_rows[last_talk_row].y + 4);
    assert(held.kind == ActionKind::hold_talkgroup && held.value == 1007);
    bool saw_last_alias = false, saw_last_id = false;
    for (size_t i = tg_begin; i < lgfx::paints.size(); ++i) {
      saw_last_alias |= lgfx::paints[i].text.find("MAXIMUM ALIAS") != std::string::npos;
      saw_last_id |= lgfx::paints[i].text.find("07") != std::string::npos;
    }
    saw_last_alias = saw_last_alias && saw_last_id;
    assert(saw_last_alias);
    tap(l.tab[3]);
    assert(view() == View::program);
    // Select the last profile, resize while preserving view and selection, then
    // prove the same profile index is returned by the production touch handler.
    const size_t page_size = static_cast<size_t>(l.profile_row_count);
    for (size_t page = 0; page < 15 / page_size; ++page) tap(l.profile_next);
    const size_t row = (15 % page_size);
    tap(l.profile_rows[row]);
    verify_changed_and_idle_update(&s);
    const Rect alternate = viewport.w > viewport.h ? Rect{72, 72, 424, 1088} : Rect{72, 72, 1088, 424};
    assert(set_viewport(alternate.x, alternate.y, alternate.w, alternate.h));
    assert(view() == View::program);
    assert(set_viewport(viewport.x, viewport.y, viewport.w, viewport.h));
    assert(view() == View::program);
    l = layout::make_layout(viewport);
    const auto selected = handle_touch(l.program_controls[0].x + l.program_controls[0].w / 2,
                                       l.program_controls[0].y + l.program_controls[0].h / 2);
    assert(selected.kind == ActionKind::select_profile && selected.value == 15);
    assert(handle_control(Control::select_profile).value == 15);
    assert(handle_control(Control::export_profile).value == 15);
    assert(handle_control(Control::delete_profile).kind == ActionKind::none);
    assert(handle_control(Control::delete_profile).kind == ActionKind::delete_profile);
    if (!l.legacy) {
      const size_t detail_begin = lgfx::paints.size();
      tap(l.program_details_tab);
      bool saw_last_channel = false;
      for (size_t i = detail_begin; i < lgfx::paints.size(); ++i)
        saw_last_channel |= lgfx::paints[i].text.find("CH 8") != std::string::npos;
      for (size_t page = 0; page < 7 / static_cast<size_t>(l.candidate_row_count); ++page)
        tap(l.program_candidate_next);
      for (size_t i = detail_begin; i < lgfx::paints.size(); ++i)
        saw_last_channel |= lgfx::paints[i].text.find("CH 8") != std::string::npos;
      assert(handle_touch(l.policy_controls[0].x + l.policy_controls[0].w / 2,
                          l.policy_controls[0].y + l.policy_controls[0].h / 2).kind == ActionKind::auto_follow_toggle);
      assert(handle_touch(l.policy_controls[1].x + l.policy_controls[1].w / 2,
                          l.policy_controls[1].y + l.policy_controls[1].h / 2).kind == ActionKind::encryption_skip_toggle);
      assert(saw_last_channel);
      tap(l.program_profiles_tab);
    } else {
      bool saw_last_channel = false;
      for (const auto& paint : lgfx::paints) saw_last_channel |= paint.text.find("CH 8") != std::string::npos;
      assert(saw_last_channel);
      assert(handle_touch(l.policy_controls[0].x + l.policy_controls[0].w / 2,
                          l.policy_controls[0].y + l.policy_controls[0].h / 2).kind == ActionKind::auto_follow_toggle);
      assert(handle_touch(l.policy_controls[1].x + l.policy_controls[1].w / 2,
                          l.policy_controls[1].y + l.policy_controls[1].h / 2).kind == ActionKind::encryption_skip_toggle);
    }
    assert(handle_touch(l.program_controls[2].x + l.program_controls[2].w / 2,
                        l.program_controls[2].y + l.program_controls[2].h / 2).kind == ActionKind::import_profile);
    const auto exported = handle_touch(l.program_controls[3].x + l.program_controls[3].w / 2,
                                       l.program_controls[3].y + l.program_controls[3].h / 2);
    assert(exported.kind == ActionKind::export_profile && exported.value == 15);
    assert(handle_touch(l.program_controls[5].x + l.program_controls[5].w / 2,
                        l.program_controls[5].y + l.program_controls[5].h / 2).kind == ActionKind::reload_config);
    const auto deletion = l.program_controls[4];
    assert(handle_touch(deletion.x + deletion.w / 2, deletion.y + deletion.h / 2).kind == ActionKind::none);
    assert(handle_touch(deletion.x + deletion.w / 2, deletion.y + deletion.h / 2).kind == ActionKind::delete_profile);
    const int clears_before_tab = lgfx::screen_clears;
    tap(l.tab[4]);
    assert(view() == View::rf_health);
    assert(lgfx::screen_clears == clears_before_tab);
    verify_changed_and_idle_update(&s);
    draw();
    if (!l.legacy) for (const auto& paint : lgfx::paints) assert(paint.text_size >= 2);
    if (!l.legacy) {
      (void)handle_control(Control::rename_profile);
      assert(g_editor_active);
      assert(std::strcmp(g_editor_value, "Stored System Profile 15") == 0);
      assert(set_viewport(alternate.x, alternate.y, alternate.w, alternate.h));
      assert(g_editor_active && std::strcmp(g_editor_value, "Stored System Profile 15") == 0);
      assert(set_viewport(viewport.x, viewport.y, viewport.w, viewport.h));
      g_editor_active = false;
      const int clears_before_controls = lgfx::screen_clears;
      const View before_next = view();
      (void)handle_control(Control::next_view);
      assert(view() != before_next);
      (void)handle_control(Control::previous_view);
      assert(view() == before_next);
      assert(lgfx::screen_clears == clears_before_controls);
    }
  }

  Snapshot empty{};
  enter(empty);
  Layout empty_layout = layout::make_layout({72, 72, 1088, 424});
  assert(set_viewport(72, 72, 1088, 424));
  lgfx::paints.clear();
  tap(empty_layout.tab[2]);
  bool saw_empty_talkgroup = false;
  for (const auto& paint : lgfx::paints)
    saw_empty_talkgroup |= paint.text.find("No active talkgroup") != std::string::npos;
  assert(saw_empty_talkgroup);
  tap(empty_layout.tab[3]);
  assert(handle_touch(empty_layout.program_controls[0].x + empty_layout.program_controls[0].w / 2,
                      empty_layout.program_controls[0].y + empty_layout.program_controls[0].h / 2).kind == ActionKind::none);
  tap(empty_layout.program_details_tab);
  assert(handle_touch(empty_layout.policy_controls[0].x + empty_layout.policy_controls[0].w / 2,
                      empty_layout.policy_controls[0].y + empty_layout.policy_controls[0].h / 2).kind == ActionKind::auto_follow_toggle);
  assert(self_check());
  std::puts("P25 production renderer/input recording test PASS");
}

// Drawing and touch regression for the Settings > Radio Defaults receiver-source rows,
// run on the host against the repo's display stub (it counts full-screen and region
// clears, as ui/AGENTS.md asks). A host simulation: it does not replace a look at the
// real panel for flicker.

#include "settings_app.hpp"
#include "text_editor.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace orcsdr;

// The shared header widgets are drawn by the application; the tests only need them to exist.
namespace orcsdr::audio_header {
void draw_brand(const char*) {}
void draw_battery(int32_t) {}
void draw_mute_button(bool) {}
void draw_visualizer_button(bool) {}
void draw_settings_button() {}
}  // namespace orcsdr::audio_header

static bool visible(const char* s) {
  return std::find(M5.Display.strings.begin(), M5.Display.strings.end(), s) != M5.Display.strings.end();
}

static settings::State base_state() {
  settings::State s;
  std::strcpy(s.default_band, "FM");
  s.fm_frequency_hz = 96100000;
  return s;
}

int main() {
  // 1. The rows draw with their defaults.
  settings::State state = base_state();
  settings::enter(state, settings::Section::radio_defaults);
  assert(visible("RECEIVER SOURCE") && visible("AUTOMATIC") && visible("CHANGE"));
  assert(visible("REMOTE SERVER") && visible("NOT SET") && visible("EDIT"));

  // 2. CHANGE cycles Automatic -> USB -> Remote -> Automatic and never clears the screen.
  const int screen_clears = M5.Display.screen_clears;
  auto action = settings::handle_touch(1070, 600);
  assert(action.kind == settings::ActionKind::receiver_source_changed && action.value == 1);
  assert(visible("LOCAL USB"));
  action = settings::handle_touch(1070, 600);
  assert(action.kind == settings::ActionKind::receiver_source_changed && action.value == 2);
  assert(visible("REMOTE RTL_TCP"));
  action = settings::handle_touch(1070, 600);
  assert(action.value == 0 && visible("AUTOMATIC"));
  assert(M5.Display.screen_clears == screen_clears);  // value changes repaint a region, not the screen

  // 3. The saved server and a coarse status show.
  state = base_state();
  state.receiver_source = 2;
  std::strcpy(state.remote_server, "192.168.1.15:5000");
  std::strcpy(state.remote_status, "REMOTE: STREAMING");
  settings::enter(state, settings::Section::radio_defaults);
  assert(visible("192.168.1.15:5000") && visible("REMOTE: STREAMING") && visible("REMOTE RTL_TCP"));

  // 4. EDIT opens the editor prefilled; CANCEL leaves the server alone.
  action = settings::handle_touch(1070, 655);
  assert(action.kind == settings::ActionKind::none && text_editor::active());
  assert(std::strcmp(text_editor::value(), "192.168.1.15:5000") == 0);
  action = settings::handle_touch(180, 653);  // CANCEL
  assert(action.kind == settings::ActionKind::none && !text_editor::active());
  char taken[72]{};
  assert(!settings::take_remote_server(taken, sizeof(taken)));

  // 5. EDIT then SAVE hands the text to the application exactly once.
  settings::handle_touch(1070, 655);
  action = settings::handle_touch(1100, 653);  // SAVE
  assert(action.kind == settings::ActionKind::remote_server_changed);
  assert(settings::take_remote_server(taken, sizeof(taken)) && std::strcmp(taken, "192.168.1.15:5000") == 0);
  assert(!settings::take_remote_server(taken, sizeof(taken)));  // consumed

  // 6. Repainting only when the status really changes.
  settings::State shown = state;
  settings::enter(shown, settings::Section::radio_defaults);
  const int regions = M5.Display.region_clears;
  settings::update(shown);                       // identical: nothing to repaint
  assert(M5.Display.region_clears == regions);
  std::strcpy(shown.remote_status, "REMOTE: BEHIND REAL TIME");
  const int before_change = M5.Display.region_clears;
  const int clears = M5.Display.screen_clears;
  settings::update(shown);                       // a real change repaints the page region
  assert(M5.Display.region_clears > before_change);
  assert(visible("REMOTE: BEHIND REAL TIME"));
  assert(M5.Display.screen_clears == clears);    // still no full-screen clear

  // 7. In USB safe mode the page shows only the recovery control, not the new rows.
  state = base_state();
  state.rtl_usb_safe_mode = true;
  settings::enter(state, settings::Section::radio_defaults);
  assert(!visible("RECEIVER SOURCE"));
  assert(settings::handle_touch(1070, 600).kind == settings::ActionKind::none);

  std::printf("SETTINGS_REMOTE_UI_TESTS pass=1\n");
  return 0;
}

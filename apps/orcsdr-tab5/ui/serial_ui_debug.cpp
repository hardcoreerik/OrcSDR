#include "serial_ui_debug.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_ui_debug {
namespace {

void say(const Hooks& hooks, const char* format, ...) {
  char line[256];
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  // UI_TAP x y: one finger press at panel coordinates, through the same path as a real touch (the virtual tap the keyboard's
  // Enter key uses), so every screen's own touch handling runs unchanged. For hands-off test automation over USB serial:
  // authenticated, one tap at a time (a new tap while one is still playing is refused), panel is 1280 x 720.
  if (strncmp(command, "UI_TAP ", 7) == 0) {
    if (!authenticated) {
      hooks.write_line("UI_TAP_ERROR auth_required");
      return true;
    }
    int x = -1, y = -1;
    char trailing = 0;
    if (sscanf(command + 7, "%d %d %c", &x, &y, &trailing) != 2 || x < 0 || x >= 1280 || y < 0 || y >= 720) {
      hooks.write_line("UI_TAP_ERROR usage: UI_TAP <x 0-1279> <y 0-719>");
      return true;
    }
    if (hooks.tap_busy()) {
      hooks.write_line("UI_TAP_ERROR busy");
      return true;
    }
    hooks.start_tap(x, y);
    say(hooks, "UI_TAP_DONE x=%d y=%d\n", x, y);
    return true;
  }
  if (strcmp(command, "RTL_TAG_DOC ON") == 0 || strcmp(command, "RTL_TAG_DOC OFF") == 0) {
    hooks.set_tag_doc(strcmp(command, "RTL_TAG_DOC ON") == 0);
    say(hooks, "RTL_TAG_DOC %s (not saved; a reset turns it off)\n", hooks.tag_doc() ? "ON" : "OFF");
    return true;
  }
  if (strcmp(command, "RTL_SCREEN_STATUS") == 0) {
    const ScreenStatus state = hooks.screen_status();
    say(hooks,
        "RTL_SCREEN_STATUS active=%s return=%s transitions=%u rejected_draws=%u visible_updates=%u "
        "last_transition_ms=%u\n",
        state.active, state.return_to, static_cast<unsigned>(state.transitions),
        static_cast<unsigned>(state.rejected_draws), static_cast<unsigned>(state.visible_updates),
        static_cast<unsigned>(state.last_transition_ms));
    return true;
  }
  if (strcmp(command, "RTL_UI_REGRESSION CHECK") == 0) {
    hooks.run_ui_regression(false);
    return true;
  }
  if (strcmp(command, "RTL_UI_REGRESSION RUN") == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_UI_REGRESSION_RESULT mode=RUN pass=0 reason=unauthenticated");
      return true;
    }
    hooks.run_ui_regression(true);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_ui_debug

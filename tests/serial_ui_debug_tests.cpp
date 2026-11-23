// Host tests for the screen test-automation serial commands (apps/orcsdr-tab5/ui/serial_ui_debug.cpp). Expected
// replies are typed from the code this replaced in main.cpp.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_ui_debug_tests.cpp apps/orcsdr-tab5/ui/serial_ui_debug.cpp
#include "serial_ui_debug.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_ui_debug;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_writes;
std::vector<std::string> g_calls;
bool g_busy = false;
bool g_tag = false;

const Hooks kHooks = [] {
  Hooks h;
  h.write = [](const char* t) { g_writes.emplace_back(t); };
  h.write_line = [](const char* t) { g_writes.emplace_back(t); g_writes.emplace_back("\n"); };
  h.tap_busy = [] { return g_busy; };
  h.start_tap = [](int x, int y) { g_calls.push_back("tap " + std::to_string(x) + " " + std::to_string(y)); };
  h.set_tag_doc = [](bool on) { g_tag = on; g_calls.push_back(on ? "tag on" : "tag off"); };
  h.tag_doc = [] { return g_tag; };
  h.screen_status = [] {
    ScreenStatus s;
    s.active = "fm";
    s.return_to = "home";
    s.transitions = 12;
    s.rejected_draws = 0;
    s.visible_updates = 4000000000u;
    s.last_transition_ms = 98765;
    return s;
  };
  h.run_ui_regression = [](bool run) { g_calls.push_back(run ? "regression run" : "regression check"); };
  return h;
}();

void reset() {
  g_writes.clear();
  g_calls.clear();
  g_busy = false;
}

std::string joined() {
  std::string s;
  for (const auto& w : g_writes) s += w;
  return s;
}

}  // namespace

int main() {
  // UI_TAP
  reset();
  CHECK(handle_command("UI_TAP 10 20", false, kHooks));
  CHECK(joined() == "UI_TAP_ERROR auth_required\n" && g_calls.empty() && g_writes.size() == 2);
  reset();
  CHECK(handle_command("UI_TAP 10 20", true, kHooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "tap 10 20" && g_writes.size() == 1 && g_writes[0] == "UI_TAP_DONE x=10 y=20\n");
  reset();
  CHECK(handle_command("UI_TAP 1279 719", true, kHooks) && g_calls.size() == 1 && g_calls[0] == "tap 1279 719");
  for (const char* bad : {"UI_TAP 1280 0", "UI_TAP 0 720", "UI_TAP -1 5", "UI_TAP 5", "UI_TAP 5 6 7", "UI_TAP a b",
                          "UI_TAP 5 6 x", "UI_TAP "}) {
    reset();
    CHECK(handle_command(bad, true, kHooks));
    CHECK(g_calls.empty() && joined() == "UI_TAP_ERROR usage: UI_TAP <x 0-1279> <y 0-719>\n");
  }
  reset();
  g_busy = true;
  CHECK(handle_command("UI_TAP 1 2", true, kHooks));
  CHECK(g_calls.empty() && joined() == "UI_TAP_ERROR busy\n");

  // RTL_TAG_DOC, no sign-in needed
  reset();
  CHECK(handle_command("RTL_TAG_DOC ON", false, kHooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "tag on" && g_writes.size() == 1 &&
        g_writes[0] == "RTL_TAG_DOC ON (not saved; a reset turns it off)\n");
  reset();
  CHECK(handle_command("RTL_TAG_DOC OFF", false, kHooks));
  CHECK(g_writes.size() == 1 && g_writes[0] == "RTL_TAG_DOC OFF (not saved; a reset turns it off)\n");

  // RTL_SCREEN_STATUS
  reset();
  CHECK(handle_command("RTL_SCREEN_STATUS", false, kHooks));
  CHECK(g_writes.size() == 1 &&
        g_writes[0] == "RTL_SCREEN_STATUS active=fm return=home transitions=12 rejected_draws=0 "
                       "visible_updates=4000000000 last_transition_ms=98765\n");

  // RTL_UI_REGRESSION
  reset();
  CHECK(handle_command("RTL_UI_REGRESSION CHECK", false, kHooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "regression check" && g_writes.empty());
  reset();
  CHECK(handle_command("RTL_UI_REGRESSION RUN", false, kHooks));
  CHECK(g_calls.empty() && joined() == "RTL_UI_REGRESSION_RESULT mode=RUN pass=0 reason=unauthenticated\n");
  reset();
  CHECK(handle_command("RTL_UI_REGRESSION RUN", true, kHooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "regression run" && g_writes.empty());

  // near misses fall through
  for (const char* other : {"UI_TAP", "UI_TAPX 1 2", "RTL_TAG_DOC", "RTL_TAG_DOC on", "RTL_SCREEN_STATUS ",
                            "RTL_UI_REGRESSION", "RTL_UI_REGRESSION run", "RTL_UI STATUS"}) {
    reset();
    CHECK(!handle_command(other, true, kHooks));
    CHECK(g_calls.empty() && g_writes.empty());
  }
  if (failures) {
    std::printf("serial_ui_debug_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("serial_ui_debug_tests: ok\n");
  return 0;
}

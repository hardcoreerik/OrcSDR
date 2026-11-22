// Host test for RTL_UI STATUS (serial_ui_status.cpp). The expected reply is the format string the dispatcher in
// main.cpp used before the family moved, filled in with the scripted facts below. Every value here is made up.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_ui_status_tests.cpp and apps/orcsdr-tab5/ui/serial_ui_status.cpp
#include "serial_ui_status.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_ui_status;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write, "L:" a line written through println.
std::vector<std::string> g_out;
int g_reads = 0;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.ui_status = []() -> UiStatus {
    ++g_reads;
    UiStatus s;
    s.screen = "fm";
    s.band = "FM";
    s.frequency_hz = 98100000u;
    s.settings = 0;
    s.fm = 1;
    s.am = 0;
    s.p25 = 0;
    s.adsb = 1;
    s.lora = 0;
    s.rf24 = 1;
    s.home_font = 1;
    s.graphics = 0;
    s.radio_ui = 1;
    s.route = "dashboard";
    s.volume = 37u;
    s.audio_enabled = 1;
    s.tray = 0;
    return s;
  };
  return hooks;
}

bool run(const char* line, bool authenticated) {
  g_out.clear();
  g_reads = 0;
  return handle_command(line, authenticated, make_hooks());
}

void test_status() {
  const char* const expected =
      "W:RTL_UI_STATUS screen=fm band=FM frequency_hz=98100000 settings=0 fm=1 am=0 p25=0 adsb=1 lora=0 rf24=1 "
      "home_font=1 graphics=0 radio_ui=1 route=dashboard volume=37 audio_enabled=1 tray=0\n";
  // No authentication needed, with or without.
  for (const bool authenticated : {false, true}) {
    CHECK(run("RTL_UI STATUS", authenticated));
    CHECK(g_out.size() == 1 && g_out[0] == expected);
    CHECK(g_reads == 1);
  }
}

void test_not_ours() {
  const char* const lines[] = {"", "RTL_UI STATUS ", "RTL_UI STATUS 1", "RTL_UI  STATUS", "rtl_ui status", "RTL_UI",
                               "RTL_UI_STATUS", "RTL_UI TAP 1 2", "RTL_UI OPEN HOME", "RTL_UI ACTION x"};
  for (const char* line : lines) {
    CHECK(!run(line, true));
    CHECK(g_out.empty());
    CHECK(g_reads == 0);
  }
}

}  // namespace

int main() {
  test_status();
  test_not_ours();
  if (failures == 0) std::printf("serial_ui_status_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}

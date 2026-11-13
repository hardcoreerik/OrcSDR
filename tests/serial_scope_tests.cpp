// Host test for the RTL_CB, RTL_FM_SCOPE and RTL_AIRBAND STATUS command family (serial_scope.cpp). The expected
// replies are the format strings the dispatcher in main.cpp used before the family moved, filled in with made-up values.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_scope_tests.cpp and apps/orcsdr-tab5/ui/serial_scope.cpp
#include "serial_scope.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_scope;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write, "L:" is a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;
int g_airband_length = 0;
bool g_scope_active = true;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.citizens_band = [](const char* command) { g_calls.push_back(std::string("cb:") + command); };
  hooks.fm_scope = []() -> FmScope {
    g_calls.push_back("fm_scope");
    FmScope scope;
    scope.active = g_scope_active;
    scope.fps = 23;
    scope.draw_ms = 41;
    return scope;
  };
  hooks.airband_status = [](char* line, size_t size) -> int {
    g_calls.push_back("airband");
    std::snprintf(line, size, "RTL_AIRBAND_STATUS made_up=1");
    return g_airband_length;
  };
  return hooks;
}

bool run(const char* command, bool authenticated = true) {
  g_out.clear();
  g_calls.clear();
  return handle_command(command, authenticated, make_hooks());
}

void test_cb() {
  CHECK(run("RTL_CB"));
  CHECK(g_calls.size() == 1 && g_calls[0] == "cb:RTL_CB");
  CHECK(g_out.empty());
  CHECK(run("RTL_CB STATUS", false));   // no authentication check here: process_cb_command decides
  CHECK(g_calls.size() == 1 && g_calls[0] == "cb:RTL_CB STATUS");
  CHECK(run("RTL_CB  "));
  CHECK(g_calls.size() == 1 && g_calls[0] == "cb:RTL_CB  ");
  // Must not match.
  CHECK(!run("RTL_CBX"));
  CHECK(!run("RTL_CB\tSTATUS"));
  CHECK(!run("RTL_C"));
  CHECK(!run("rtl_cb"));
  CHECK(g_calls.empty() && g_out.empty());
}

void test_fm_scope() {
  g_scope_active = true;
  CHECK(run("RTL_FM_SCOPE"));
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_FM_SCOPE_STATUS active=1 fps=23 draw_ms=41\n");
  g_scope_active = false;
  CHECK(run("RTL_FM_SCOPE", false));
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_FM_SCOPE_STATUS active=0 fps=23 draw_ms=41\n");
  g_scope_active = true;
  CHECK(!run("RTL_FM_SCOPE "));
  CHECK(!run("RTL_FM_SCOPE STATUS"));
  CHECK(!run("RTL_FM_SCOP"));
  CHECK(g_calls.empty() && g_out.empty());
}

void test_airband() {
  g_airband_length = 27;
  CHECK(run("RTL_AIRBAND STATUS"));
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_AIRBAND_STATUS made_up=1");
  g_airband_length = 0;   // no line: nothing is printed, but the command was still handled
  CHECK(run("RTL_AIRBAND STATUS", false));
  CHECK(g_out.empty());
  CHECK(g_calls.size() == 1 && g_calls[0] == "airband");
  g_airband_length = -1;
  CHECK(run("RTL_AIRBAND STATUS"));
  CHECK(g_out.empty());
  g_airband_length = 27;
  CHECK(!run("RTL_AIRBAND"));
  CHECK(!run("RTL_AIRBAND STATUS "));
  CHECK(!run("RTL_AIRBAND status"));
  CHECK(g_calls.empty() && g_out.empty());
}

void test_other_commands() {
  CHECK(!run("RTL_KEYBOARD STATUS"));
  CHECK(!run(""));
  CHECK(!run("RTL_UI STATUS"));
  CHECK(g_calls.empty() && g_out.empty());
}

}  // namespace

int main() {
  test_cb();
  test_fm_scope();
  test_airband();
  test_other_commands();
  if (failures == 0) std::printf("serial_scope tests passed\n");
  return failures == 0 ? 0 : 1;
}

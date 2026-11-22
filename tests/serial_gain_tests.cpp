// Host test for the RTL_GAIN, RTL_AM_GAIN and RTL_FM_GAIN command family (serial_gain.cpp). The expected replies are
// the strings the dispatcher in main.cpp used before the family moved.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_gain_tests.cpp apps/orcsdr-tab5/ui/serial_gain.cpp
#include "serial_gain.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_gain;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write, "L:" is a line written through println; g_calls records the device actions in order.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;
int g_gain_now = 0;

void call(const std::string& text) { g_calls.push_back(text); }

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.print_status = []() { call("status"); };
  hooks.set_auto = []() { call("auto"); };
  hooks.set_manual = [](int gain) { call("manual " + std::to_string(gain)); };
  hooks.set_rtl_agc = [](bool enabled) { call(enabled ? "agc on" : "agc off"); };
  hooks.current_gain = []() -> int { call("current_gain"); return g_gain_now; };
  hooks.refresh_ui = []() { call("refresh"); };
  hooks.am_status = []() -> AutoStatus { return {true, false, 279, -24.0, 0.1234}; };
  hooks.fm_status = []() -> AutoStatus { return {false, true, -5, -24.0, 12.0}; };
  return hooks;
}

void reset() {
  g_out.clear();
  g_calls.clear();
}

void expect_out(const std::vector<std::string>& want) {
  if (g_out != want) {
    std::printf("FAIL: output differs\n");
    for (const auto& line : g_out) std::printf("  got  %s\n", line.c_str());
    for (const auto& line : want) std::printf("  want %s\n", line.c_str());
    ++failures;
  }
  reset();
}

void expect_calls(const std::vector<std::string>& want) {
  if (g_calls != want) {
    std::printf("FAIL: calls differ\n");
    for (const auto& line : g_calls) std::printf("  got  %s\n", line.c_str());
    for (const auto& line : want) std::printf("  want %s\n", line.c_str());
    ++failures;
  }
  reset();
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // STATUS needs no authentication and the hook prints the reply itself.
  CHECK(handle_command("RTL_GAIN STATUS", false, hooks));
  expect_calls({"status"});
  CHECK(g_out.empty());
  CHECK(handle_command("RTL_GAIN STATUS", true, hooks));
  expect_calls({"status"});

  // Every RTL_GAIN action is refused for an unauthenticated caller before anything is called.
  for (const char* line : {"RTL_GAIN AUTO", "RTL_GAIN SMART", "RTL_GAIN MANUAL", "RTL_GAIN MANUAL 100", "RTL_GAIN RTLAGC ON",
                           "RTL_GAIN nonsense", "RTL_GAIN "}) {
    CHECK(handle_command(line, false, hooks));
    CHECK(g_calls.empty());
    expect_out({"L:RTL_GAIN_ERROR auth_required"});
  }

  // AUTO and SMART take the same path; the UI refresh follows the change.
  CHECK(handle_command("RTL_GAIN AUTO", true, hooks));
  expect_calls({"auto", "refresh"});
  CHECK(handle_command("RTL_GAIN SMART", true, hooks));
  expect_calls({"auto", "refresh"});

  // MANUAL with a value: 0..496 accepted, parsed with strtol (so a sign or leading space is the library's business).
  CHECK(handle_command("RTL_GAIN MANUAL 0", true, hooks));
  expect_calls({"manual 0", "refresh"});
  CHECK(handle_command("RTL_GAIN MANUAL 496", true, hooks));
  expect_calls({"manual 496", "refresh"});
  CHECK(handle_command("RTL_GAIN MANUAL 279", true, hooks));
  expect_calls({"manual 279", "refresh"});
  CHECK(handle_command("RTL_GAIN MANUAL +12", true, hooks));
  expect_calls({"manual 12", "refresh"});
  CHECK(handle_command("RTL_GAIN MANUAL  15", true, hooks));   // strtol skips the second space
  expect_calls({"manual 15", "refresh"});

  // MANUAL with no value holds the current gain (read first, then set).
  g_gain_now = 207;
  CHECK(handle_command("RTL_GAIN MANUAL", true, hooks));
  expect_calls({"current_gain", "manual 207", "refresh"});
  g_gain_now = 0;
  CHECK(handle_command("RTL_GAIN MANUAL", true, hooks));
  expect_calls({"current_gain", "manual 0", "refresh"});

  // MANUAL range and parse errors change nothing and do not refresh the UI.
  for (const char* bad : {"RTL_GAIN MANUAL -1", "RTL_GAIN MANUAL 497", "RTL_GAIN MANUAL x", "RTL_GAIN MANUAL 12x",
                          "RTL_GAIN MANUAL ", "RTL_GAIN MANUAL 1 2", "RTL_GAIN MANUAL 99999999999999999999"}) {
    CHECK(handle_command(bad, true, hooks));
    CHECK(g_calls.empty());
    expect_out({"L:RTL_GAIN_INVALID use MANUAL [0..496]"});
  }

  // RTLAGC.
  CHECK(handle_command("RTL_GAIN RTLAGC ON", true, hooks));
  expect_calls({"agc on", "refresh"});
  CHECK(handle_command("RTL_GAIN RTLAGC OFF", true, hooks));
  expect_calls({"agc off", "refresh"});

  // Anything else after the prefix is invalid (exact words, case sensitive, no trailing text).
  for (const char* bad : {"RTL_GAIN ", "RTL_GAIN auto", "RTL_GAIN AUTO ", "RTL_GAIN AUTOX", "RTL_GAIN MANUALX",
                          "RTL_GAIN MANUAL5", "RTL_GAIN RTLAGC", "RTL_GAIN RTLAGC ON ", "RTL_GAIN RTLAGC MAYBE", "RTL_GAIN STATUS ",
                          "RTL_GAIN STATUSX", "RTL_GAIN GAIN 5"}) {
    CHECK(handle_command(bad, true, hooks));
    CHECK(g_calls.empty());
    expect_out({"L:RTL_GAIN_INVALID use STATUS|AUTO|SMART|MANUAL [0..496]|RTLAGC ON|OFF"});
  }

  // AM and FM status: one write each, one decimal for the target, three for the clipping.
  CHECK(handle_command("RTL_AM_GAIN STATUS", false, hooks));
  expect_out({"W:RTL_AM_GAIN_STATUS mode=AUTO selecting=0 gain_tenth_db=279 target_dbfs=-24.0 clipping_percent=0.123\n"});
  CHECK(handle_command("RTL_FM_GAIN STATUS", false, hooks));
  expect_out({"W:RTL_FM_GAIN_STATUS mode=MANUAL selecting=1 gain_tenth_db=-5 target_dbfs=-24.0 clipping_percent=12.000\n"});
  CHECK(g_calls.empty());

  // Lines that are not ours fall through to the dispatcher: exact routes stay exact, the prefix needs its space.
  for (const char* other : {"RTL_GAIN", "RTL_GAINS", "RTL_GAIN_STATUS", "RTL_AM_GAIN", "RTL_AM_GAIN STATUS ", "RTL_AM_GAIN  STATUS",
                            "RTL_FM_GAIN", "RTL_FM_GAIN STATUSX", "rtl_gain status", "RTL_AM_GAIN AUTO", "RTL_FM_GAIN AUTO",
                            "RTL_DRIVER STATUS", ""}) {
    CHECK(!handle_command(other, true, hooks));
  }
  CHECK(g_out.empty() && g_calls.empty());

  if (failures == 0) std::printf("PASS: RTL_GAIN, RTL_AM_GAIN and RTL_FM_GAIN replies, refusals, ranges and call order\n");
  return failures == 0 ? 0 : 1;
}

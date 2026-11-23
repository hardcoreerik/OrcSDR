// Host test for the RTL_WEB, RTL_WEB_STATUS, RTL_WEB_AUDIO_STATUS and RTL_WEB ON|OFF command family (serial_web.cpp).
// The expected replies are the strings the dispatcher in main.cpp used before the family moved. All addresses here
// are made-up documentation values.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_web_tests.cpp apps/orcsdr-tab5/ui/serial_web.cpp
#include "serial_web.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_web;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_out;
std::vector<std::string> g_calls;
Status g_status;        // what status() reports
Status g_applied;       // what apply_enabled() reports

void call(const std::string& text) { g_calls.push_back(text); }

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.status = []() -> Status { call("status"); return g_status; };
  hooks.print_audio_status = []() { call("audio status"); };
  hooks.apply_enabled = [](bool enable) -> Status {
    call(enable ? "apply on" : "apply off");
    return g_applied;
  };
  hooks.update_global_settings = []() { call("update settings"); };
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

Status make_status(bool enabled, bool listening, const char* url) {
  Status status;
  status.enabled = enabled;
  status.listening = listening;
  std::strncpy(status.url, url, sizeof(status.url) - 1);
  return status;
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // RTL_WEB and RTL_WEB_STATUS: the same reply, no authentication needed; an empty url prints "offline".
  g_status = make_status(true, true, "http://192.0.2.10/");
  for (const char* line : {"RTL_WEB", "RTL_WEB_STATUS"}) {
    for (bool auth : {false, true}) {
      CHECK(handle_command(line, auth, hooks));
      expect_calls({"status"});
    }
  }
  CHECK(handle_command("RTL_WEB", false, hooks));
  expect_out({"W:RTL_WEB_STATUS enabled=1 listening=1 url=http://192.0.2.10/\n"});
  g_status = make_status(true, false, "");
  CHECK(handle_command("RTL_WEB_STATUS", true, hooks));
  expect_out({"W:RTL_WEB_STATUS enabled=1 listening=0 url=offline\n"});
  g_status = make_status(false, false, "");
  CHECK(handle_command("RTL_WEB", true, hooks));
  expect_out({"W:RTL_WEB_STATUS enabled=0 listening=0 url=offline\n"});

  // The audio status prints its own reply, in either auth state.
  for (bool auth : {false, true}) {
    CHECK(handle_command("RTL_WEB_AUDIO_STATUS", auth, hooks));
    expect_calls({"audio status"});
    CHECK(g_out.empty());
  }

  // ON / OFF need authentication: unauthenticated callers get no reply and nothing is called (not ours).
  for (const char* line : {"RTL_WEB ON", "RTL_WEB OFF"}) {
    CHECK(!handle_command(line, false, hooks));
    CHECK(g_out.empty() && g_calls.empty());
  }

  // Authenticated: apply, then the reply, then the global settings update, in that order.
  g_applied = make_status(true, true, "http://192.0.2.10/");
  CHECK(handle_command("RTL_WEB ON", true, hooks));
  expect_calls({"apply on", "update settings"});
  CHECK(handle_command("RTL_WEB ON", true, hooks));
  expect_out({"W:RTL_WEB_OK enabled=1 listening=1 url=http://192.0.2.10/\n"});
  g_applied = make_status(false, false, "");
  CHECK(handle_command("RTL_WEB OFF", true, hooks));
  expect_calls({"apply off", "update settings"});
  CHECK(handle_command("RTL_WEB OFF", true, hooks));
  expect_out({"W:RTL_WEB_OK enabled=0 listening=0 url=offline\n"});
  g_applied = make_status(true, false, "");
  CHECK(handle_command("RTL_WEB ON", true, hooks));
  expect_out({"W:RTL_WEB_OK enabled=1 listening=0 url=offline\n"});

  // Lines that are not ours fall through: exact routes stay exact.
  for (const char* other : {"RTL_WEB ", "RTL_WEB ON ", "RTL_WEB on", "RTL_WEB MAYBE", "RTL_WEBS", "RTL_WEB_STATUS ",
                            "RTL_WEB_AUDIO_STATUS ", "RTL_WEB_AUDIO", "rtl_web", "RTL_WEB  ON", "RTL_VOLUME", ""}) {
    CHECK(!handle_command(other, true, hooks));
    CHECK(!handle_command(other, false, hooks));
  }
  CHECK(g_out.empty() && g_calls.empty());

  if (failures == 0) std::printf("PASS: RTL_WEB replies, the authentication gate on ON/OFF, call order and exact matching\n");
  return failures == 0 ? 0 : 1;
}

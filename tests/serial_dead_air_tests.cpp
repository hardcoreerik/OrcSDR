// Host test for the RTL_DEAD_AIR command family (serial_dead_air.cpp). The family only routes: every line it prints
// except the two refusals and the FLOOD reply is written by dead_air_app, which the fake hooks below stand in for.
// The refusals and the FLOOD format are the strings the dispatcher in main.cpp used before the family moved.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_dead_air_tests.cpp and apps/orcsdr-tab5/ui/serial_dead_air.cpp
#include "serial_dead_air.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_dead_air;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// Every write is recorded separately, so the test also pins how the reply is split into writes:
// "W:" is one verbatim write (it carries its own '\n'), "L:" is a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;   // each hook call with the arguments it was given
uint32_t g_flood_result = 0;
bool g_veil_result = true;

// Stands in for a dead_air_app text function: records the buffer size it was offered and fills in a line.
void fill(const char* name, const char* text, char* out, size_t size) {
  g_calls.push_back(std::string(name) + " size=" + std::to_string(size));
  std::snprintf(out, size, "%s", text);
}

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.flood_manual_marks = [](uint32_t count) -> uint32_t {
    g_calls.push_back("flood count=" + std::to_string(count));
    return g_flood_result;
  };
  hooks.describe = [](char* out, size_t size) { fill("describe", "RTL_DEAD_AIR_STATUS open=1 fake", out, size); };
  hooks.describe_veil = [](char* out, size_t size) { fill("veil", "RTL_DEAD_AIR_VEIL on=0 fake", out, size); };
  hooks.veil_command = [](const char* args, char* reply, size_t size) -> bool {
    g_calls.push_back(std::string("veil_command args=[") + args + "] size=" + std::to_string(size));
    std::snprintf(reply, size, "%s", g_veil_result ? "RTL_DEAD_AIR_VEIL_OK fake" : "RTL_DEAD_AIR_VEIL_ERROR fake");
    return g_veil_result;
  };
  hooks.describe_env = [](char* out, size_t size) { fill("env", "RTL_DEAD_AIR_ENV present=1 fake", out, size); };
  return hooks;
}

void expect(const std::vector<std::string>& calls, const std::vector<std::string>& expected) {
  if (g_out != expected || g_calls != calls) {
    ++failures;
    std::printf("FAIL output or calls:\n");
    for (const auto& line : g_calls) std::printf("  call: %s\n", line.c_str());
    for (size_t i = 0; i < g_out.size() || i < expected.size(); ++i) {
      std::printf("  got:  %s  want: %s", i < g_out.size() ? g_out[i].c_str() : "<nothing>\n",
                  i < expected.size() ? expected[i].c_str() : "<nothing>\n");
      if ((i < g_out.size() ? g_out[i] : "x").back() != '\n') std::printf("\n");
    }
  }
  g_out.clear();
  g_calls.clear();
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // STATUS, VEIL and ENV need no authentication. Each offers the buffer the dispatcher offered (768, 560 and 420
  // bytes) and prints what came back as one line.
  for (const bool authenticated : {false, true}) {
    CHECK(handle_command("RTL_DEAD_AIR STATUS", authenticated, hooks));
    expect({"describe size=768"}, {"L:RTL_DEAD_AIR_STATUS open=1 fake"});
    CHECK(handle_command("RTL_DEAD_AIR VEIL", authenticated, hooks));
    expect({"veil size=560"}, {"L:RTL_DEAD_AIR_VEIL on=0 fake"});
    CHECK(handle_command("RTL_DEAD_AIR ENV", authenticated, hooks));
    expect({"env size=420"}, {"L:RTL_DEAD_AIR_ENV present=1 fake"});
  }

  // FLOOD and the VEIL command refuse an unauthenticated caller before calling anything, whatever the argument.
  for (const char* line : {"RTL_DEAD_AIR FLOOD 0", "RTL_DEAD_AIR FLOOD 500", "RTL_DEAD_AIR FLOOD ", "RTL_DEAD_AIR FLOOD x"}) {
    CHECK(handle_command(line, false, hooks));
    expect({}, {"L:RTL_DEAD_AIR_FLOOD_ERROR auth_required"});
  }
  for (const char* line : {"RTL_DEAD_AIR VEIL 0", "RTL_DEAD_AIR VEIL ON", "RTL_DEAD_AIR VEIL ", "RTL_DEAD_AIR VEIL INTENT x"}) {
    CHECK(handle_command(line, false, hooks));
    expect({}, {"L:RTL_DEAD_AIR_VEIL_ERROR auth_required"});
  }

  // FLOOD: the count is read with atoi (no range check here; dead_air_app decides) and the reply is what came back.
  g_flood_result = 37;
  CHECK(handle_command("RTL_DEAD_AIR FLOOD 500", true, hooks));
  expect({"flood count=500"}, {"W:RTL_DEAD_AIR_FLOOD logged=37\n"});
  g_flood_result = 0;   // Dead Air not open
  CHECK(handle_command("RTL_DEAD_AIR FLOOD 12abc", true, hooks));
  expect({"flood count=12"}, {"W:RTL_DEAD_AIR_FLOOD logged=0\n"});
  // Not a number, or nothing: atoi gives 0 and the call is still made.
  for (const char* line : {"RTL_DEAD_AIR FLOOD x", "RTL_DEAD_AIR FLOOD ", "RTL_DEAD_AIR FLOOD 0"}) {
    CHECK(handle_command(line, true, hooks));
    expect({"flood count=0"}, {"W:RTL_DEAD_AIR_FLOOD logged=0\n"});
  }
  // A negative count wraps to a large unsigned one, as it did in the dispatcher.
  CHECK(handle_command("RTL_DEAD_AIR FLOOD -1", true, hooks));
  expect({"flood count=4294967295"}, {"W:RTL_DEAD_AIR_FLOOD logged=0\n"});
  g_flood_result = 4000000000u;   // printed unsigned
  CHECK(handle_command("RTL_DEAD_AIR FLOOD  7", true, hooks));
  expect({"flood count=7"}, {"W:RTL_DEAD_AIR_FLOOD logged=4000000000\n"});

  // VEIL <args>: everything after the space is passed on unchanged with a 160-byte reply buffer, and the reply is
  // printed whether the command succeeded or not.
  CHECK(handle_command("RTL_DEAD_AIR VEIL ON", true, hooks));
  expect({"veil_command args=[ON] size=160"}, {"L:RTL_DEAD_AIR_VEIL_OK fake"});
  CHECK(handle_command("RTL_DEAD_AIR VEIL INTENT  two  words ", true, hooks));
  expect({"veil_command args=[INTENT  two  words ] size=160"}, {"L:RTL_DEAD_AIR_VEIL_OK fake"});
  g_veil_result = false;
  CHECK(handle_command("RTL_DEAD_AIR VEIL nonsense", true, hooks));
  expect({"veil_command args=[nonsense] size=160"}, {"L:RTL_DEAD_AIR_VEIL_ERROR fake"});
  // "VEIL " with nothing after the space is the command route (empty argument), not the report.
  CHECK(handle_command("RTL_DEAD_AIR VEIL ", true, hooks));
  expect({"veil_command args=[] size=160"}, {"L:RTL_DEAD_AIR_VEIL_ERROR fake"});

  // Anything else is left to the dispatcher: STATUS and ENV are exact, FLOOD needs its trailing space.
  for (const char* other : {"RTL_DEAD_AIR", "RTL_DEAD_AIR ", "RTL_DEAD_AIR STATUS ", "RTL_DEAD_AIR STATUSX",
                            "RTL_DEAD_AIR ENV ", "RTL_DEAD_AIR ENV 1", "RTL_DEAD_AIR FLOOD", "RTL_DEAD_AIR VEILS",
                            "RTL_DEAD_AIR MARK", "rtl_dead_air status", "RTL_DEAD_AIR_STATUS", "RTL_ACARS STATUS", ""}) {
    CHECK(!handle_command(other, true, hooks));
    CHECK(!handle_command(other, false, hooks));
  }
  CHECK(g_out.empty() && g_calls.empty());

  if (failures == 0) std::printf("PASS: RTL_DEAD_AIR FLOOD, STATUS, VEIL and ENV routing, refusals, arguments and buffer sizes\n");
  return failures == 0 ? 0 : 1;
}

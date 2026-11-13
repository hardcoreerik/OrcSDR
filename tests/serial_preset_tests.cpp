// Host test for the RTL_PRESET_LIST / RTL_PRESET_TUNE command family (serial_preset.cpp). The expected replies are
// the strings the dispatcher in main.cpp used before the family moved. The frequencies are made-up host values.
//
// The dispatcher compared 17 bytes against the 16-character "RTL_PRESET_TUNE " (so it also compared the terminator)
// and read the index from command + 17. The module keeps that exactly; the test therefore builds lines in a padded
// buffer the way the firmware's command buffer is: the terminator at [16] and the digits behind it.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_preset_tests.cpp and apps/orcsdr-tab5/ui/serial_preset.cpp
#include "serial_preset.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_preset;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_out;
std::vector<std::string> g_calls;
int g_count = 3;
const Preset kPresets[] = {{88100000u, -41.25f}, {93300000u, -50.0f}, {101500000u, -62.5f}};

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.preset_count = []() -> int { return g_count; };
  hooks.preset = [](int index) -> Preset { return kPresets[index]; };
  hooks.tune_fm = [](uint32_t hz) { g_calls.push_back("tune:" + std::to_string(hz)); };
  hooks.persist_fm = [](uint32_t hz) { g_calls.push_back("persist:" + std::to_string(hz)); };
  return hooks;
}

void reset() {
  g_out.clear();
  g_calls.clear();
  g_count = 3;
}

bool run(const char* line, bool authenticated) {
  const Hooks hooks = make_hooks();
  return handle_command(line, authenticated, hooks);
}

// "RTL_PRESET_TUNE " + NUL + digits, as the 17-byte compare in the original needs.
bool run_tune(const char* digits, bool authenticated) {
  char buffer[64] = {};
  std::memcpy(buffer, "RTL_PRESET_TUNE ", 16);
  buffer[16] = '\0';
  std::strncpy(buffer + 17, digits, sizeof(buffer) - 18);
  return run(buffer, authenticated);
}

}  // namespace

int main() {
  reset();
  CHECK(run("RTL_PRESET_LIST", false));
  CHECK(g_out.size() == 5);
  CHECK(g_out.size() == 5 && g_out[0] == "W:RTL_PRESET_LIST_BEGIN count=3\n");
  CHECK(g_out.size() == 5 && g_out[1] == "W:RTL_PRESET 1 frequency_hz=88100000 level=-41.2\n");
  CHECK(g_out.size() == 5 && g_out[2] == "W:RTL_PRESET 2 frequency_hz=93300000 level=-50.0\n");
  CHECK(g_out.size() == 5 && g_out[3] == "W:RTL_PRESET 3 frequency_hz=101500000 level=-62.5\n");
  CHECK(g_out.size() == 5 && g_out[4] == "L:RTL_PRESET_LIST_END");

  reset();
  g_count = 0;
  CHECK(run("RTL_PRESET_LIST", true));
  CHECK(g_out.size() == 2 && g_out[0] == "W:RTL_PRESET_LIST_BEGIN count=0\n" && g_out[1] == "L:RTL_PRESET_LIST_END");

  // Tune: first preset, third preset; order is tune, persist, reply.
  reset();
  CHECK(run_tune("1", true));
  CHECK(g_calls.size() == 2 && g_calls[0] == "tune:88100000" && g_calls[1] == "persist:88100000");
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_PRESET_TUNE_OK index=1 frequency_hz=88100000\n");
  reset();
  CHECK(run_tune("3", true));
  CHECK(g_calls.size() == 2 && g_calls[0] == "tune:101500000" && g_calls[1] == "persist:101500000");
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_PRESET_TUNE_OK index=3 frequency_hz=101500000\n");

  // Range errors: 0, one past the end, negative, not a number, empty.
  const char* const bad[] = {"0", "4", "-2", "abc", ""};
  for (const char* digits : bad) {
    reset();
    CHECK(run_tune(digits, true));
    CHECK(g_calls.empty());
    CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_PRESET_TUNE_INVALID index out of range");
  }
  reset();
  g_count = 0;
  CHECK(run_tune("1", true));
  CHECK(g_calls.empty() && g_out.size() == 1 && g_out[0] == "L:RTL_PRESET_TUNE_INVALID index out of range");

  // Not authenticated: not ours, nothing called.
  reset();
  CHECK(!run_tune("1", false));
  CHECK(g_calls.empty() && g_out.empty());

  // Because of the 17-byte compare, a line with the index right behind the space does not match.
  reset();
  CHECK(!run("RTL_PRESET_TUNE 1", true));
  CHECK(!run("RTL_PRESET_TUNE", true));
  CHECK(!run("RTL_PRESET_LIST ", true));
  CHECK(!run("RTL_PRESET_LISTS", true));
  CHECK(!run("RTL_PRESET", true));
  CHECK(!run("RTL_PRESET_SCAN", true));
  CHECK(!run("", true));
  CHECK(g_calls.empty() && g_out.empty());

  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("serial_preset tests passed\n");
  return 0;
}

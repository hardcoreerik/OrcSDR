// Host test for the RTL_AUDIO_TEST command family (serial_audio_test.cpp). Each route only starts its hook, which
// prints the reply itself, as the dispatcher in main.cpp did before the family moved.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_audio_test_tests.cpp apps/orcsdr-tab5/ui/serial_audio_test.cpp
#include "serial_audio_test.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_audio_test;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_calls;

void call(const std::string& text) { g_calls.push_back(text); }

Hooks make_hooks() {
  Hooks hooks;
  hooks.test_status = []() { call("test status"); };
  hooks.test_tone = []() { call("test tone"); };
  hooks.test_fm = []() { call("test fm"); };
  hooks.test_stop = []() { call("test stop"); };
  return hooks;
}

void expect_calls(const std::vector<std::string>& want) {
  if (g_calls != want) {
    std::printf("FAIL: calls differ\n");
    for (const auto& line : g_calls) std::printf("  got  %s\n", line.c_str());
    for (const auto& line : want) std::printf("  want %s\n", line.c_str());
    ++failures;
  }
  g_calls.clear();
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // No authentication check on these routes: both auth states run the one hook, once.
  for (bool auth : {false, true}) {
    CHECK(handle_command("RTL_AUDIO_TEST STATUS", auth, hooks));
    expect_calls({"test status"});
    CHECK(handle_command("RTL_AUDIO_TEST TONE", auth, hooks));
    expect_calls({"test tone"});
    CHECK(handle_command("RTL_AUDIO_TEST FM", auth, hooks));
    expect_calls({"test fm"});
    CHECK(handle_command("RTL_AUDIO_TEST STOP", auth, hooks));
    expect_calls({"test stop"});
  }

  // Lines that are not ours fall through: exact routes stay exact (case, spacing, trailing text).
  for (const char* other : {"RTL_AUDIO_TEST", "RTL_AUDIO_TEST ", "RTL_AUDIO_TEST STATUS ", "RTL_AUDIO_TEST status",
                            "RTL_AUDIO_TEST tone", "RTL_AUDIO_TEST TONEX", "RTL_AUDIO_TEST STOPX", "RTL_AUDIO_TEST  FM",
                            "RTL_AUDIO_TESTS STATUS", "rtl_audio_test stop", "RTL_SOUND", "RTL_VOLUME", ""}) {
    CHECK(!handle_command(other, true, hooks));
    CHECK(!handle_command(other, false, hooks));
  }
  CHECK(g_calls.empty());

  if (failures == 0) std::printf("PASS: RTL_AUDIO_TEST routes call their hook once and match exactly\n");
  return failures == 0 ? 0 : 1;
}

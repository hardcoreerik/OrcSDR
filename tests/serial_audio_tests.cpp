// Host test for the RTL_VOLUME and RTL_SOUND command family (serial_audio.cpp). The expected replies
// are the strings the dispatcher in main.cpp used before the family moved.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_audio_tests.cpp apps/orcsdr-tab5/ui/serial_audio.cpp
#include "serial_audio.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_audio;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_out;
std::vector<std::string> g_calls;
uint8_t g_volume = 0;
bool g_sound = false;

void call(const std::string& text) { g_calls.push_back(text); }

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.volume = []() -> uint8_t { call("volume"); return g_volume; };
  hooks.sound_enabled = []() -> bool { call("sound_enabled"); return g_sound; };
  hooks.set_sound_enabled = [](bool enable) { call(enable ? "sound on" : "sound off"); };
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

  // Volume: one write, unsigned decimal, same for both auth states (no auth check on these routes).
  for (bool auth : {false, true}) {
    g_volume = 0;
    CHECK(handle_command("RTL_VOLUME", auth, hooks));
    expect_out({"W:RTL_VOLUME_STATUS volume=0\n"});
    g_volume = 255;
    CHECK(handle_command("RTL_VOLUME", auth, hooks));
    expect_out({"W:RTL_VOLUME_STATUS volume=255\n"});
  }
  g_volume = 40;
  CHECK(handle_command("RTL_VOLUME", true, hooks));
  expect_calls({"volume"});

  // Sound status.
  g_sound = true;
  CHECK(handle_command("RTL_SOUND", false, hooks));
  expect_out({"W:RTL_SOUND_STATUS enabled=1\n"});
  g_sound = false;
  CHECK(handle_command("RTL_SOUND", true, hooks));
  expect_out({"W:RTL_SOUND_STATUS enabled=0\n"});

  // Sound ON / OFF: the action runs before the reply, no authentication needed.
  CHECK(handle_command("RTL_SOUND ON", false, hooks));
  expect_out({"W:RTL_SOUND_OK enabled=1\n"});
  CHECK(handle_command("RTL_SOUND ON", true, hooks));
  expect_calls({"sound on"});
  CHECK(handle_command("RTL_SOUND OFF", false, hooks));
  expect_out({"W:RTL_SOUND_OK enabled=0\n"});
  CHECK(handle_command("RTL_SOUND OFF", true, hooks));
  expect_calls({"sound off"});

  // Lines that are not ours fall through: exact routes stay exact (case, spacing, trailing text).
  for (const char* other : {"RTL_VOLUME ", "RTL_VOLUME 50", "RTL_VOLUMES", "rtl_volume", "RTL_SOUND ", "RTL_SOUND ON ",
                            "RTL_SOUND on", "RTL_SOUND MAYBE", "RTL_SOUNDS", "RTL_SOUND_STATUS", "RTL_AUDIO_TEST",
                            "RTL_AUDIO_TEST STATUS", "RTL_AUDIO_TEST STOP", "RTL_SIGNAL", "RTL_WEB", ""}) {
    CHECK(!handle_command(other, true, hooks));
    CHECK(!handle_command(other, false, hooks));
  }
  CHECK(g_out.empty() && g_calls.empty());

  if (failures == 0) std::printf("PASS: RTL_VOLUME and RTL_SOUND replies, hook order and exact matching\n");
  return failures == 0 ? 0 : 1;
}

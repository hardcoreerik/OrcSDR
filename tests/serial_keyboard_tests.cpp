// Host test for the RTL_KEYBOARD command family (serial_keyboard.cpp). The expected replies are the format strings
// the dispatcher in main.cpp used before the family moved, filled in with the scripted device facts below.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_keyboard_tests.cpp and apps/orcsdr-tab5/ui/serial_keyboard.cpp
#include "serial_keyboard.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_keyboard;
using orcsdr::keyboard_input::Key;
using orcsdr::keyboard_input::Special;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// Every write is recorded separately, so the test also pins how the reply is split into writes:
// "W:" is one verbatim write (it carries its own '\n'), "L:" is a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;
std::vector<Key> g_keys;
bool g_echo = false;
bool g_editor = false;
bool g_focused = true;
bool g_ring = true;
bool g_present = true;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.status = []() -> orcsdr::tab5_keyboard::Status {
    g_calls.push_back("status");
    orcsdr::tab5_keyboard::Status status;
    if (!g_present) return status;
    status.present = true;
    status.firmware = 3;
    status.mode = 2;
    status.int_low = true;
    status.keys = 4000000000u;   // above INT32_MAX: the counters are printed unsigned
    status.dropped = 12;
    status.undecoded = 7;
    status.bus_errors = 5;
    status.attach_count = 2;
    return status;
  };
  hooks.echo = []() -> bool { return g_echo; };
  hooks.editor_active = []() -> bool { return g_editor; };
  hooks.focus = []() -> Focus {
    g_calls.push_back("focus");
    Focus focus;
    focus.focused = g_focused;
    focus.controls = 9;
    if (g_focused) {
      focus.x = 24;
      focus.y = -6;
      focus.w = 320;
      focus.h = 72;
      focus.ring = g_ring;
    }
    return focus;
  };
  hooks.route_key = [](const Key& key) {
    g_calls.push_back("key");
    g_keys.push_back(key);
  };
  hooks.set_echo = [](bool on) -> bool {
    g_calls.push_back(on ? "echo1" : "echo0");
    g_echo = on;
    return g_echo;
  };
  return hooks;
}

void expect(const std::vector<std::string>& expected) {
  if (g_out != expected) {
    ++failures;
    std::printf("FAIL output:\n");
    for (size_t i = 0; i < g_out.size() || i < expected.size(); ++i) {
      std::printf("  got:  %s  want: %s", i < g_out.size() ? g_out[i].c_str() : "<nothing>\n",
                  i < expected.size() ? expected[i].c_str() : "<nothing>\n");
      if ((i < g_out.size() ? g_out[i] : "x").back() != '\n') std::printf("\n");
    }
  }
  g_out.clear();
  g_calls.clear();
}

// The one key the last command delivered.
bool delivered(Special special, char ch) {
  const bool ok = g_calls.size() == 1 && g_calls[0] == "key" && g_keys.size() == 1 && g_keys[0].special == special &&
                  g_keys[0].ch == ch && !g_keys[0].ctrl && !g_keys[0].alt;
  g_keys.clear();
  return ok;
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // STATUS needs no authentication: the status line, then the focus line.
  CHECK(handle_command("RTL_KEYBOARD STATUS", false, hooks));
  CHECK(g_calls.size() == 2 && g_calls[0] == "status" && g_calls[1] == "focus");
  expect({"W:RTL_KEYBOARD_STATUS present=1 firmware=3 mode=2 int_low=1 keys=4000000000 dropped=12 undecoded=7 "
          "bus_errors=5 attaches=2 echo=0 editor=0\n",
          "W:RTL_KEYBOARD_FOCUS controls=9 x=24 y=-6 w=320 h=72 ring=1\n"});
  g_echo = true;
  g_editor = true;
  g_ring = false;
  CHECK(handle_command("RTL_KEYBOARD STATUS", true, hooks));
  expect({"W:RTL_KEYBOARD_STATUS present=1 firmware=3 mode=2 int_low=1 keys=4000000000 dropped=12 undecoded=7 "
          "bus_errors=5 attaches=2 echo=1 editor=1\n",
          "W:RTL_KEYBOARD_FOCUS controls=9 x=24 y=-6 w=320 h=72 ring=0\n"});
  // No keyboard and nothing focused.
  g_echo = false;
  g_editor = false;
  g_present = false;
  g_focused = false;
  CHECK(handle_command("RTL_KEYBOARD STATUS", false, hooks));
  expect({"W:RTL_KEYBOARD_STATUS present=0 firmware=0 mode=0 int_low=0 keys=0 dropped=0 undecoded=0 "
          "bus_errors=0 attaches=0 echo=0 editor=0\n",
          "W:RTL_KEYBOARD_FOCUS controls=9 none\n"});

  // KEY and ECHO refuse an unauthenticated caller before doing anything, even for a name that is not a key.
  for (const char* line : {"RTL_KEYBOARD KEY UP", "RTL_KEYBOARD KEY 0", "RTL_KEYBOARD KEY ", "RTL_KEYBOARD KEY nonsense",
                           "RTL_KEYBOARD ECHO 1", "RTL_KEYBOARD ECHO 0", "RTL_KEYBOARD ECHO "}) {
    CHECK(handle_command(line, false, hooks));
    CHECK(g_calls.empty() && g_keys.empty() && !g_echo);
    expect({"L:RTL_KEYBOARD_ERROR auth_required"});
  }

  // KEY: the named keys.
  const struct { const char* line; Special special; char ch; } named[] = {
      {"RTL_KEYBOARD KEY UP", Special::up, '\0'},
      {"RTL_KEYBOARD KEY DOWN", Special::down, '\0'},
      {"RTL_KEYBOARD KEY LEFT", Special::left, '\0'},
      {"RTL_KEYBOARD KEY RIGHT", Special::right, '\0'},
      {"RTL_KEYBOARD KEY ENTER", Special::enter, '\0'},
      {"RTL_KEYBOARD KEY ESC", Special::escape, '\0'},
      {"RTL_KEYBOARD KEY BACKSPACE", Special::backspace, '\0'},
      {"RTL_KEYBOARD KEY TAB", Special::tab, '\0'},
      {"RTL_KEYBOARD KEY SPACE", Special::none, ' '},
      // Any single character is typed as itself, a space included.
      {"RTL_KEYBOARD KEY a", Special::none, 'a'},
      {"RTL_KEYBOARD KEY Z", Special::none, 'Z'},
      {"RTL_KEYBOARD KEY 7", Special::none, '7'},
      {"RTL_KEYBOARD KEY  ", Special::none, ' '},
      {"RTL_KEYBOARD KEY |", Special::none, '|'},
  };
  for (const auto& entry : named) {
    CHECK(handle_command(entry.line, true, hooks));
    CHECK(delivered(entry.special, entry.ch));
    expect({"L:RTL_KEYBOARD_KEY_OK"});   // printed after the key was delivered
  }
  // KEY: names are exact and upper case; nothing is delivered for anything else.
  for (const char* bad : {"RTL_KEYBOARD KEY ", "RTL_KEYBOARD KEY up", "RTL_KEYBOARD KEY UP ", "RTL_KEYBOARD KEY ESCAPE",
                          "RTL_KEYBOARD KEY DELETE", "RTL_KEYBOARD KEY ab", "RTL_KEYBOARD KEY  UP"}) {
    CHECK(handle_command(bad, true, hooks));
    CHECK(g_calls.empty() && g_keys.empty());
    expect({"L:RTL_KEYBOARD_INVALID use UP|DOWN|LEFT|RIGHT|ENTER|ESC|TAB|SPACE|BACKSPACE|<char>"});
  }

  // ECHO: only a '1' right after the space switches it on; everything else, an empty argument included, is off.
  CHECK(handle_command("RTL_KEYBOARD ECHO 1", true, hooks));
  CHECK(g_echo && g_calls.size() == 1 && g_calls[0] == "echo1");
  expect({"W:RTL_KEYBOARD_ECHO 1\n"});
  CHECK(handle_command("RTL_KEYBOARD ECHO 0", true, hooks));
  CHECK(!g_echo && g_calls.size() == 1 && g_calls[0] == "echo0");
  expect({"W:RTL_KEYBOARD_ECHO 0\n"});
  CHECK(handle_command("RTL_KEYBOARD ECHO 10", true, hooks));
  CHECK(g_echo);
  expect({"W:RTL_KEYBOARD_ECHO 1\n"});
  for (const char* off : {"RTL_KEYBOARD ECHO ", "RTL_KEYBOARD ECHO ON", "RTL_KEYBOARD ECHO  1", "RTL_KEYBOARD ECHO 2"}) {
    g_echo = true;
    CHECK(handle_command(off, true, hooks));
    CHECK(!g_echo);
    expect({"W:RTL_KEYBOARD_ECHO 0\n"});
  }

  // Anything else is left to the dispatcher: STATUS is exact, KEY and ECHO need their trailing space.
  for (const char* other : {"RTL_KEYBOARD", "RTL_KEYBOARD ", "RTL_KEYBOARD STATUS ", "RTL_KEYBOARD STATUSX",
                            "RTL_KEYBOARD KEY", "RTL_KEYBOARD ECHO", "RTL_KEYBOARD KEYS", "rtl_keyboard status",
                            "RTL_KEYBOARD_STATUS", "RTL_UI STATUS", ""}) {
    CHECK(!handle_command(other, true, hooks));
    CHECK(!handle_command(other, false, hooks));
  }
  CHECK(g_out.empty() && g_calls.empty() && g_keys.empty());

  if (failures == 0) std::printf("PASS: RTL_KEYBOARD STATUS, KEY and ECHO replies, refusals, key names and call order\n");
  return failures == 0 ? 0 : 1;
}

// Host tests for two small serial families moved out of main.cpp: the JS8/FT8 prefixes (serial_digimodes.cpp) and
// RTL_SPLASH_GATE (serial_splash.cpp). Expected behaviour is typed from the code this replaced.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_small_families_tests.cpp apps/orcsdr-tab5/ui/serial_digimodes.cpp
//   apps/orcsdr-tab5/ui/serial_splash.cpp
#include <cstdio>
#include <string>
#include <vector>

#include "serial_digimodes.hpp"
#include "serial_splash.hpp"

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_writes;
std::vector<std::string> g_calls;
bool g_gate = true;

const orcsdr::serial_digimodes::Hooks kDigi = [] {
  orcsdr::serial_digimodes::Hooks h;
  h.js8 = [](const char* a) { g_calls.push_back(std::string("js8[") + a + "]"); };
  h.ft8 = [](const char* a) { g_calls.push_back(std::string("ft8[") + a + "]"); };
  return h;
}();

const orcsdr::serial_splash::Hooks kSplash = [] {
  orcsdr::serial_splash::Hooks h;
  h.write = [](const char* t) { g_writes.emplace_back(t); };
  h.write_line = [](const char* t) { g_writes.emplace_back(t); g_writes.emplace_back("\n"); };
  h.store_gate = [](bool on) { g_gate = on; g_calls.push_back(on ? "store on" : "store off"); };
  h.gate = [] { return g_gate; };
  return h;
}();

void reset() {
  g_writes.clear();
  g_calls.clear();
}

std::string joined() {
  std::string s;
  for (const auto& w : g_writes) s += w;
  return s;
}

}  // namespace

int main() {
  using orcsdr::serial_digimodes::handle_command;
  // JS8 / FT8: the rest of the line, from the space, goes to the parser; signed in or not.
  for (bool auth : {false, true}) {
    reset();
    CHECK(handle_command("JS8", auth, kDigi) && g_calls.size() == 1 && g_calls[0] == "js8[]");
    reset();
    CHECK(handle_command("JS8 STATUS", auth, kDigi) && g_calls.size() == 1 && g_calls[0] == "js8[ STATUS]");
    reset();
    CHECK(handle_command("FT8", auth, kDigi) && g_calls.size() == 1 && g_calls[0] == "ft8[]");
    reset();
    CHECK(handle_command("FT8 SNAP", auth, kDigi) && g_calls.size() == 1 && g_calls[0] == "ft8[ SNAP]");
  }
  for (const char* other : {"JS8X", "FT8_", "JS", "ft8", "FT8\tX", "RTL_FT8"}) {
    reset();
    CHECK(!handle_command(other, true, kDigi) && g_calls.empty());
  }

  using orcsdr::serial_splash::handle_command;
  // Status needs no sign-in, in any of its spellings.
  for (const char* c : {"RTL_SPLASH_GATE", "RTL_SPLASH_GATE STATUS", "RTL_SPLASH_GATESTATUS"}) {
    reset();
    g_gate = true;
    CHECK(handle_command(c, false, kSplash) && g_calls.empty() && g_writes.size() == 1 &&
          g_writes[0] == "RTL_SPLASH_GATE enabled=1\n");
  }
  // Changes need it; the reply then shows the stored value.
  reset();
  CHECK(handle_command("RTL_SPLASH_GATE OFF", false, kSplash) && g_calls.empty() &&
        joined() == "RTL_SPLASH_GATE_ERROR auth_required\n");
  reset();
  CHECK(handle_command("RTL_SPLASH_GATE OFF", true, kSplash) && g_calls.size() == 1 && g_calls[0] == "store off" &&
        g_writes.size() == 1 && g_writes[0] == "RTL_SPLASH_GATE enabled=0\n");
  reset();
  CHECK(handle_command("RTL_SPLASH_GATE ON", true, kSplash) && g_calls[0] == "store on" &&
        g_writes[0] == "RTL_SPLASH_GATE enabled=1\n");
  reset();
  CHECK(handle_command("RTL_SPLASH_GATEON", true, kSplash) && g_calls.size() == 1 && g_calls[0] == "store on");
  for (const char* bad : {"RTL_SPLASH_GATE on", "RTL_SPLASH_GATE X", "RTL_SPLASH_GATE  ON", "RTL_SPLASH_GATES"}) {
    reset();
    CHECK(handle_command(bad, true, kSplash) && g_calls.empty() &&
          joined() == "RTL_SPLASH_GATE_INVALID use RTL_SPLASH_GATE [ON|OFF|STATUS]\n");
  }
  reset();
  CHECK(!handle_command("RTL_SPLASH", true, kSplash) && g_writes.empty());

  if (failures) {
    std::printf("serial_small_families_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("serial_small_families_tests: ok\n");
  return 0;
}

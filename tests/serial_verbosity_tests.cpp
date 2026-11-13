// Host test for the RTL_SERIAL VERBOSITY command family (serial_verbosity.cpp). The expected replies are the strings
// the dispatcher in main.cpp used before the family moved. The fake parse and name hooks follow the device's
// (QUIET=0, NORMAL=1, DEBUG=2, TRACE=3, matched without regard to case).
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_verbosity_tests.cpp and apps/orcsdr-tab5/ui/serial_verbosity.cpp
#include "serial_verbosity.hpp"

#include <strings.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_verbosity;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_out;
std::vector<std::string> g_calls;
uint8_t g_level = 1;

const char* level_name(uint8_t level) {
  switch (level) {
    case 0: return "QUIET";
    case 1: return "NORMAL";
    case 2: return "DEBUG";
    case 3: return "TRACE";
  }
  return "NORMAL";
}

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.current = []() -> uint8_t { g_calls.push_back("current"); return g_level; };
  hooks.name = [](uint8_t level) -> const char* { return level_name(level); };
  hooks.parse = [](const char* text, uint8_t* level) -> bool {
    g_calls.push_back(std::string("parse:") + text);
    for (uint8_t i = 0; i < 4; ++i) {
      if (strcasecmp(text, level_name(i)) == 0) {
        *level = i;
        return true;
      }
    }
    return false;
  };
  hooks.save_and_apply = [](uint8_t level) {
    g_calls.push_back(std::string("save:") + std::to_string(level));
    g_level = level;
  };
  return hooks;
}

bool run(const char* command, bool authenticated = true) {
  g_out.clear();
  g_calls.clear();
  return handle_command(command, authenticated, make_hooks());
}

void test_read() {
  g_level = 1;
  CHECK(run("RTL_SERIAL VERBOSITY", false));   // reading needs no authentication
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_SERIAL_VERBOSITY mode=NORMAL\n");
  g_level = 3;
  CHECK(run("RTL_SERIAL VERBOSITY"));
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_SERIAL_VERBOSITY mode=TRACE\n");
  g_level = 0;
  CHECK(run("RTL_SERIAL VERBOSITY"));
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_SERIAL_VERBOSITY mode=QUIET\n");
}

void test_set() {
  g_level = 1;
  CHECK(run("RTL_SERIAL VERBOSITY DEBUG"));
  CHECK(g_calls.size() == 2 && g_calls[0] == "parse:DEBUG" && g_calls[1] == "save:2");   // saved before the reply
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_SERIAL_VERBOSITY_OK mode=DEBUG\n");
  CHECK(g_level == 2);
  CHECK(run("RTL_SERIAL VERBOSITY quiet"));
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_SERIAL_VERBOSITY_OK mode=QUIET\n");
  CHECK(run("RTL_SERIAL VERBOSITY Trace"));
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_SERIAL_VERBOSITY_OK mode=TRACE\n");
  CHECK(g_level == 3);
}

void test_refusals() {
  g_level = 1;
  // Unauthenticated: refused before anything is parsed or stored, for a valid and an invalid level alike.
  CHECK(run("RTL_SERIAL VERBOSITY DEBUG", false));
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_SERIAL_VERBOSITY_ERROR auth_required");
  CHECK(g_calls.empty());
  CHECK(run("RTL_SERIAL VERBOSITY nonsense", false));
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_SERIAL_VERBOSITY_ERROR auth_required");
  CHECK(g_calls.empty());
  CHECK(run("RTL_SERIAL VERBOSITY ", false));   // the trailing space alone is already the set route
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_SERIAL_VERBOSITY_ERROR auth_required");
  CHECK(g_level == 1);
  // Authenticated but not a level: nothing stored.
  CHECK(run("RTL_SERIAL VERBOSITY LOUD"));
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_SERIAL_VERBOSITY_INVALID use QUIET|NORMAL|DEBUG|TRACE");
  CHECK(g_calls.size() == 1 && g_calls[0] == "parse:LOUD");
  CHECK(run("RTL_SERIAL VERBOSITY "));
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_SERIAL_VERBOSITY_INVALID use QUIET|NORMAL|DEBUG|TRACE");
  CHECK(g_calls.size() == 1 && g_calls[0] == "parse:");
  CHECK(run("RTL_SERIAL VERBOSITY  DEBUG"));   // two spaces: the text handed to parse starts with a space
  CHECK(g_out.size() == 1 && g_out[0].rfind("L:RTL_SERIAL_VERBOSITY_INVALID", 0) == 0);
  CHECK(g_calls.size() == 1 && g_calls[0] == "parse: DEBUG");
  CHECK(g_level == 1);
}

void test_no_match() {
  CHECK(!run("RTL_SERIAL"));
  CHECK(!run("RTL_SERIAL VERBOSIT"));
  CHECK(!run("RTL_SERIAL VERBOSITYX"));
  CHECK(!run("RTL_SERIAL STATUS"));
  CHECK(!run("rtl_serial verbosity"));
  CHECK(!run(""));
  CHECK(g_calls.empty() && g_out.empty());
}

}  // namespace

int main() {
  test_read();
  test_set();
  test_refusals();
  test_no_match();
  if (failures == 0) std::printf("serial_verbosity tests passed\n");
  return failures == 0 ? 0 : 1;
}

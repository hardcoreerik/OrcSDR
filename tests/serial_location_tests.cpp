// Host test for the RTL_LOCATION command family (serial_location.cpp). The expected replies are the format strings the
// dispatcher in main.cpp used before the family moved, filled in with the scripted estimate below.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_location_tests.cpp and apps/orcsdr-tab5/ui/serial_location.cpp
#include "serial_location.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_location;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write (it carries its own '\n'), "L:" is a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;
bool g_queue_result = true;    // what the request hooks return
bool g_ready_after_confirm = true;
bool g_confirmed = false;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.state = []() -> orcsdr::location_estimate::State {
    g_calls.push_back("state");
    orcsdr::location_estimate::State state;
    state.busy = true;
    state.ready = g_confirmed ? g_ready_after_confirm : true;
    state.latitude_e7 = -123456789;   // made-up position
    state.longitude_e7 = 987654321;
    std::strcpy(state.message, "Found Exampleton");
    return state;
  };
  hooks.request_ip = []() -> bool { g_calls.push_back("ip"); return g_queue_result; };
  hooks.request_lookup = [](const char* query) -> bool {
    g_calls.push_back(std::string("lookup:") + query);
    return g_queue_result;
  };
  hooks.confirm = [] { g_calls.push_back("confirm"); g_confirmed = true; };
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
}

void reset() {
  g_out.clear();
  g_calls.clear();
  g_queue_result = true;
  g_ready_after_confirm = true;
  g_confirmed = false;
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // STATUS needs no authentication.
  reset();
  CHECK(handle_command("RTL_LOCATION STATUS", false, hooks));
  expect({"W:RTL_LOCATION_STATUS busy=1 ready=1 latitude_e7=-123456789 longitude_e7=987654321 message=\"Found Exampleton\"\n"});
  CHECK(g_calls.size() == 1 && g_calls[0] == "state");

  // IP, LOOKUP and CONFIRM refuse an unauthenticated caller before doing anything.
  for (const char* command : {"RTL_LOCATION IP", "RTL_LOCATION LOOKUP Exampleton", "RTL_LOCATION LOOKUP ", "RTL_LOCATION CONFIRM"}) {
    reset();
    CHECK(handle_command(command, false, hooks));
    CHECK(g_calls.empty());
    expect({"L:RTL_LOCATION_ERROR auth_required"});
  }

  // IP.
  reset();
  CHECK(handle_command("RTL_LOCATION IP", true, hooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "ip");
  expect({"W:RTL_LOCATION_IP_QUEUED\n"});
  reset();
  g_queue_result = false;
  CHECK(handle_command("RTL_LOCATION IP", true, hooks));
  expect({"W:RTL_LOCATION_IP_REJECTED\n"});

  // LOOKUP passes everything after the prefix, spaces included.
  reset();
  CHECK(handle_command("RTL_LOCATION LOOKUP Exampleton, Testland", true, hooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "lookup:Exampleton, Testland");
  expect({"W:RTL_LOCATION_LOOKUP_QUEUED\n"});
  reset();
  g_queue_result = false;
  CHECK(handle_command("RTL_LOCATION LOOKUP Exampleton", true, hooks));
  expect({"W:RTL_LOCATION_LOOKUP_REJECTED\n"});

  // LOOKUP query length: empty and 64 or more characters are refused without a request; 63 is accepted.
  reset();
  CHECK(handle_command("RTL_LOCATION LOOKUP ", true, hooks));
  CHECK(g_calls.empty());
  expect({"L:RTL_LOCATION_ERROR query_length"});
  const std::string long64(64, 'x');
  CHECK(handle_command(("RTL_LOCATION LOOKUP " + long64).c_str(), true, hooks));
  CHECK(g_calls.empty());
  expect({"L:RTL_LOCATION_ERROR query_length"});
  const std::string ok63(63, 'x');
  CHECK(handle_command(("RTL_LOCATION LOOKUP " + ok63).c_str(), true, hooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "lookup:" + ok63);
  expect({"W:RTL_LOCATION_LOOKUP_QUEUED\n"});

  // CONFIRM applies first, then reports whether the estimate is ready afterwards.
  reset();
  CHECK(handle_command("RTL_LOCATION CONFIRM", true, hooks));
  CHECK(g_calls.size() == 2 && g_calls[0] == "confirm" && g_calls[1] == "state");
  expect({"W:RTL_LOCATION_CONFIRM_OK\n"});
  reset();
  g_ready_after_confirm = false;
  CHECK(handle_command("RTL_LOCATION CONFIRM", true, hooks));
  expect({"W:RTL_LOCATION_CONFIRM_REJECTED\n"});

  // Lines that are not this family's: exact match, and LOOKUP needs its trailing space.
  reset();
  for (const char* other : {"RTL_LOCATION", "RTL_LOCATION STATUS ", "RTL_LOCATION IP ", "RTL_LOCATION CONFIRM now",
                            "RTL_LOCATION LOOKUP", "RTL_LOCATION  STATUS", "rtl_location status", "RTL_LOCATION_STATUS",
                            "RTL_LOCATIONS", ""}) {
    CHECK(!handle_command(other, true, hooks));
  }
  CHECK(g_out.empty() && g_calls.empty());

  if (failures == 0) std::printf("serial_location_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}

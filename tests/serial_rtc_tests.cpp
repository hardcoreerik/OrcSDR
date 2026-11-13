// Host test for the clock command family (serial_rtc.cpp). The expected replies are the format strings the
// dispatcher in main.cpp used before the family moved, filled in with the scripted facts below. Every timestamp
// here is made up.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_rtc_tests.cpp and apps/orcsdr-tab5/ui/serial_rtc.cpp
#include "serial_rtc.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_rtc;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write (it carries its own '\n'), "L:" a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;

Clock g_clock;
bool g_store_ok = true;
bool g_set_ok = true;
bool g_estimated_ok = true;
int32_t g_stored = 0;
uint32_t g_set_value = 0;

bool valid_offset(int32_t minutes) {
  g_calls.push_back("valid_offset");
  return minutes >= -720 && minutes <= 840;   // made-up limits for the test
}

const Hooks kHooks{
    [](const char* text) { g_out.push_back(std::string("W:") + text); },
    [](const char* text) { g_out.push_back(std::string("L:") + text); },
    [] { g_calls.push_back("clock"); return g_clock; },
    valid_offset,
    [](int32_t minutes) { g_calls.push_back("store_offset"); g_stored = minutes; return g_store_ok; },
    [](uint32_t utc) { g_calls.push_back("set_utc"); g_set_value = utc; return g_set_ok; },
    [] { g_calls.push_back("forget"); },
    [] { g_calls.push_back("mark_estimated"); return g_estimated_ok; },
    [] { g_calls.push_back("changed"); },
};

void reset() {
  g_out.clear();
  g_calls.clear();
  g_store_ok = g_set_ok = g_estimated_ok = true;
  g_stored = 0;
  g_set_value = 0;
}

bool out_is(std::initializer_list<const char*> expected) {
  if (g_out.size() != expected.size()) return false;
  size_t i = 0;
  for (const char* e : expected) if (g_out[i++] != e) return false;
  return true;
}

bool calls_are(std::initializer_list<const char*> expected) {
  if (g_calls.size() != expected.size()) return false;
  size_t i = 0;
  for (const char* e : expected) if (g_calls[i++] != e) return false;
  return true;
}

}  // namespace

int main() {
  // STATUS: no authentication needed; one write.
  g_clock = Clock{true, 1700000000u, true, 60};
  CHECK(handle_command("ORC_RTC_STATUS", false, kHooks));
  CHECK(out_is({"W:ORC_RTC_STATUS valid=1 utc=1700000000 source=hardware offset_min=60\n"}));
  reset();
  g_clock = Clock{false, 0u, false, -300};
  CHECK(handle_command("ORC_RTC_STATUS", true, kHooks));
  CHECK(out_is({"W:ORC_RTC_STATUS valid=0 utc=0 source=unavailable offset_min=-300\n"}));
  reset();

  // TZ_SET refused without authentication, nothing called.
  CHECK(handle_command("ORC_TZ_SET 60", false, kHooks));
  CHECK(out_is({"L:ORC_TZ_SET_ERROR auth_required"}) && g_calls.empty());
  reset();
  // TZ_SET happy path: validate, store, reply, then refresh.
  CHECK(handle_command("ORC_TZ_SET 60", true, kHooks));
  CHECK(out_is({"W:ORC_TZ_SET_OK offset_min=60\n"}));
  CHECK(calls_are({"valid_offset", "store_offset", "changed"}) && g_stored == 60);
  reset();
  CHECK(handle_command("ORC_TZ_SET -300", true, kHooks));
  CHECK(out_is({"W:ORC_TZ_SET_OK offset_min=-300\n"}) && g_stored == -300);
  reset();
  // Parse errors: the offset check is never asked.
  for (const char* bad : {"ORC_TZ_SET ", "ORC_TZ_SET abc", "ORC_TZ_SET 60x", "ORC_TZ_SET 60 "}) {
    CHECK(handle_command(bad, true, kHooks));
    CHECK(out_is({"L:ORC_TZ_SET_ERROR out_of_range"}));
    CHECK(g_calls.empty());
    reset();
  }
  // Parsed but invalid: valid_offset is asked, nothing stored.
  CHECK(handle_command("ORC_TZ_SET 9999", true, kHooks));
  CHECK(out_is({"L:ORC_TZ_SET_ERROR out_of_range"}) && calls_are({"valid_offset"}));
  reset();
  // Store failure: no OK, no refresh.
  g_store_ok = false;
  CHECK(handle_command("ORC_TZ_SET 60", true, kHooks));
  CHECK(out_is({"L:ORC_TZ_SET_ERROR persistence_failed"}) && calls_are({"valid_offset", "store_offset"}));
  reset();

  // RTC_SET refused without authentication.
  CHECK(handle_command("ORC_RTC_SET 1700000000", false, kHooks));
  CHECK(out_is({"L:ORC_RTC_SET_ERROR auth_required"}) && g_calls.empty());
  reset();
  // Happy path: set, forget trust, save flag, reply, refresh.
  CHECK(handle_command("ORC_RTC_SET 1700000000", true, kHooks));
  CHECK(out_is({"W:ORC_RTC_SET_OK utc=1700000000\n"}));
  CHECK(calls_are({"set_utc", "forget", "mark_estimated", "changed"}) && g_set_value == 1700000000u);
  reset();
  // Parse errors never reach set_utc.
  for (const char* bad : {"ORC_RTC_SET ", "ORC_RTC_SET abc", "ORC_RTC_SET 17000x", "ORC_RTC_SET 1700000000 "}) {
    CHECK(handle_command(bad, true, kHooks));
    CHECK(out_is({"L:ORC_RTC_SET_ERROR invalid_or_unavailable"}));
    CHECK(g_calls.empty());
    reset();
  }
  // Rejected by the clock: nothing else runs.
  g_set_ok = false;
  CHECK(handle_command("ORC_RTC_SET 1700000000", true, kHooks));
  CHECK(out_is({"L:ORC_RTC_SET_ERROR invalid_or_unavailable"}) && calls_are({"set_utc"}));
  reset();
  // Flag not saved: trust was already forgotten, no OK, no refresh.
  g_estimated_ok = false;
  CHECK(handle_command("ORC_RTC_SET 1700000000", true, kHooks));
  CHECK(out_is({"L:ORC_RTC_SET_ERROR persistence_failed"}) && calls_are({"set_utc", "forget", "mark_estimated"}));
  reset();

  // Anything else is left to the dispatcher: exact matches stay exact, the setters need their trailing space.
  for (const char* other : {"ORC_RTC_STATUS ", "ORC_RTC_STATUSX", "ORC_RTC_STATU", "ORC_TZ_SET", "ORC_RTC_SET", "ORC_TZ_SETX 1",
                            "orc_rtc_status", "ORC_RTC", "RTL_RESET", ""}) {
    CHECK(!handle_command(other, true, kHooks));
  }
  CHECK(g_out.empty() && g_calls.empty());

  if (failures == 0) std::printf("PASS: ORC_RTC_STATUS, ORC_TZ_SET and ORC_RTC_SET replies, refusals, parse errors and call order\n");
  return failures == 0 ? 0 : 1;
}

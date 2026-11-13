// Host test for the RTL_POCSAG command family (serial_pocsag.cpp). The expected replies are the strings the
// dispatcher in main.cpp used before the family moved.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_pocsag_tests.cpp and apps/orcsdr-tab5/ui/serial_pocsag.cpp
#include "serial_pocsag.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_pocsag;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// Every write and every device action is recorded in one list, in the order it happened, so the test pins both how
// the reply is split into writes and that the action comes before the reply:
// "W:" is one verbatim write (it carries its own '\n'), "L:" is a line written through println, "C:" is a hook call.
std::vector<std::string> g_log;
bool g_on_band = true;
uint32_t g_clamp_to = 0;   // 0: tune() returns what it was given

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_log.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_log.push_back(std::string("L:") + text); };
  hooks.print_status = [] { g_log.push_back("C:status"); };
  hooks.on_pocsag_band = []() -> bool { return g_on_band; };
  hooks.request_scan = [] { g_log.push_back("C:scan"); };
  hooks.request_scan_stop = [] { g_log.push_back("C:scan_stop"); };
  hooks.set_baud = [](uint16_t baud) { g_log.push_back("C:baud " + std::to_string(baud)); };
  hooks.set_polarity = [](uint8_t mode) { g_log.push_back("C:polarity " + std::to_string(mode)); };
  hooks.tune = [](uint32_t hz) -> uint32_t {
    g_log.push_back("C:tune " + std::to_string(hz));
    return g_clamp_to != 0 ? g_clamp_to : hz;
  };
  return hooks;
}

void expect(int line, const std::vector<std::string>& expected) {
  if (g_log != expected) {
    ++failures;
    std::printf("FAIL line %d output:\n", line);
    for (size_t i = 0; i < g_log.size() || i < expected.size(); ++i) {
      std::printf("  got:  [%s]  want: [%s]\n", i < g_log.size() ? g_log[i].c_str() : "<nothing>",
                  i < expected.size() ? expected[i].c_str() : "<nothing>");
    }
  }
  g_log.clear();
}
#define EXPECT(...) expect(__LINE__, __VA_ARGS__)

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // STATUS needs no authentication.
  CHECK(handle_command("RTL_POCSAG STATUS", false, hooks));
  EXPECT({"C:status"});
  CHECK(handle_command("RTL_POCSAG STATUS", true, hooks));
  EXPECT({"C:status"});

  // Every other route has `authenticated` in its condition: without it the line is not this family's, nothing is
  // printed and nothing is done, so the rest of the dispatcher still sees the line.
  for (const char* line : {"RTL_POCSAG_SCAN", "RTL_POCSAG_SCAN_STOP", "RTL_POCSAG_SET_BAUD 1200", "RTL_POCSAG_SET_BAUD x",
                           "RTL_POCSAG_SET_POLARITY AUTO", "RTL_POCSAG_SET_POLARITY x", "RTL_POCSAG_TUNE 152240000",
                           "RTL_POCSAG_TUNE x"}) {
    CHECK(!handle_command(line, false, hooks));
    CHECK(g_log.empty());
    g_log.clear();
  }

  // SCAN: only on the POCSAG band.
  g_on_band = false;
  CHECK(handle_command("RTL_POCSAG_SCAN", true, hooks));
  EXPECT({"L:RTL_POCSAG_SCAN_INVALID POCSAG band only"});
  g_on_band = true;
  CHECK(handle_command("RTL_POCSAG_SCAN", true, hooks));
  EXPECT({"C:scan", "L:RTL_POCSAG_SCAN_QUEUED"});

  // SCAN_STOP does not look at the band.
  g_on_band = false;
  CHECK(handle_command("RTL_POCSAG_SCAN_STOP", true, hooks));
  EXPECT({"C:scan_stop", "L:RTL_POCSAG_SCAN_STOP_QUEUED"});
  g_on_band = true;

  // SET_BAUD: AUTO in any case (always echoed as AUTO), the three rates exactly as typed.
  CHECK(handle_command("RTL_POCSAG_SET_BAUD AUTO", true, hooks));
  EXPECT({"C:baud 0", "W:RTL_POCSAG_SET_BAUD_OK baud=AUTO\n"});
  CHECK(handle_command("RTL_POCSAG_SET_BAUD auto", true, hooks));
  EXPECT({"C:baud 0", "W:RTL_POCSAG_SET_BAUD_OK baud=AUTO\n"});
  CHECK(handle_command("RTL_POCSAG_SET_BAUD 512", true, hooks));
  EXPECT({"C:baud 512", "W:RTL_POCSAG_SET_BAUD_OK baud=512\n"});
  CHECK(handle_command("RTL_POCSAG_SET_BAUD 1200", true, hooks));
  EXPECT({"C:baud 1200", "W:RTL_POCSAG_SET_BAUD_OK baud=1200\n"});
  CHECK(handle_command("RTL_POCSAG_SET_BAUD 2400", true, hooks));
  EXPECT({"C:baud 2400", "W:RTL_POCSAG_SET_BAUD_OK baud=2400\n"});
  for (const char* bad : {"RTL_POCSAG_SET_BAUD ", "RTL_POCSAG_SET_BAUD 0", "RTL_POCSAG_SET_BAUD 600",
                          "RTL_POCSAG_SET_BAUD 1200 ", "RTL_POCSAG_SET_BAUD  1200", "RTL_POCSAG_SET_BAUD 01200",
                          "RTL_POCSAG_SET_BAUD AUTOX"}) {
    CHECK(handle_command(bad, true, hooks));
    EXPECT({"L:RTL_POCSAG_SET_BAUD_INVALID use AUTO|512|1200|2400"});
  }

  // SET_POLARITY: three words in any case, echoed in capitals.
  CHECK(handle_command("RTL_POCSAG_SET_POLARITY AUTO", true, hooks));
  EXPECT({"C:polarity 0", "W:RTL_POCSAG_SET_POLARITY_OK polarity=AUTO\n"});
  CHECK(handle_command("RTL_POCSAG_SET_POLARITY normal", true, hooks));
  EXPECT({"C:polarity 1", "W:RTL_POCSAG_SET_POLARITY_OK polarity=NORMAL\n"});
  CHECK(handle_command("RTL_POCSAG_SET_POLARITY Inverted", true, hooks));
  EXPECT({"C:polarity 2", "W:RTL_POCSAG_SET_POLARITY_OK polarity=INVERTED\n"});
  for (const char* bad : {"RTL_POCSAG_SET_POLARITY ", "RTL_POCSAG_SET_POLARITY 1", "RTL_POCSAG_SET_POLARITY NORMAL ",
                          "RTL_POCSAG_SET_POLARITY INVERT"}) {
    CHECK(handle_command(bad, true, hooks));
    EXPECT({"L:RTL_POCSAG_SET_POLARITY_INVALID use AUTO|NORMAL|INVERTED"});
  }

  // TUNE: a decimal number that is not zero; the reply shows the clamped frequency, not the one asked for.
  CHECK(handle_command("RTL_POCSAG_TUNE 152240000", true, hooks));
  EXPECT({"C:tune 152240000", "W:RTL_POCSAG_TUNE_OK frequency_hz=152240000\n"});
  g_clamp_to = 137000000;
  CHECK(handle_command("RTL_POCSAG_TUNE 5", true, hooks));
  EXPECT({"C:tune 5", "W:RTL_POCSAG_TUNE_OK frequency_hz=137000000\n"});
  g_clamp_to = 0;
  // strtoul rules are kept: leading spaces and trailing text are accepted.
  CHECK(handle_command("RTL_POCSAG_TUNE  929612500 Hz", true, hooks));
  EXPECT({"C:tune 929612500", "W:RTL_POCSAG_TUNE_OK frequency_hz=929612500\n"});
  for (const char* bad : {"RTL_POCSAG_TUNE ", "RTL_POCSAG_TUNE 0", "RTL_POCSAG_TUNE x", "RTL_POCSAG_TUNE 000",
                          "RTL_POCSAG_TUNE MHz"}) {
    CHECK(handle_command(bad, true, hooks));
    EXPECT({"L:RTL_POCSAG_TUNE_INVALID usage: RTL_POCSAG_TUNE <HZ>"});
  }

  // Anything else is left to the dispatcher: exact matches stay exact, the prefixes need their trailing space.
  for (const char* other : {"RTL_POCSAG", "RTL_POCSAG ", "RTL_POCSAG STATUS ", "RTL_POCSAG STATUSX", "RTL_POCSAG_STATUS",
                            "rtl_pocsag status", "RTL_POCSAG_SCAN ", "RTL_POCSAG_SCAN_STOP ", "RTL_POCSAG_SCAN_START",
                            "RTL_POCSAG_SET_BAUD", "RTL_POCSAG_SET_POLARITY", "RTL_POCSAG_TUNE", "RTL_POCSAG_TUNE_152240000",
                            "RTL_PRESET_LIST", "RTL_PRESET_SCAN", ""}) {
    CHECK(!handle_command(other, true, hooks));
    CHECK(!handle_command(other, false, hooks));
  }
  CHECK(g_log.empty());

  if (failures == 0) {
    std::printf("PASS: RTL_POCSAG STATUS, SCAN, SCAN_STOP, SET_BAUD, SET_POLARITY and TUNE replies, silent "
                "unauthenticated fall-through, parse errors and call order\n");
  }
  return failures == 0 ? 0 : 1;
}

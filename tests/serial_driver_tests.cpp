// Host test for the RTL_DRIVER SELF_CHECK and RTL_DRIVER <action> commands (serial_driver.cpp). The expected replies
// are the strings the dispatcher in main.cpp used before the family moved.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_driver_tests.cpp apps/orcsdr-tab5/ui/serial_driver.cpp
#include "serial_driver.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_driver;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write, "L:" is a line written through println; g_calls records the driver actions in order.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;
bool g_installed = true;
bool g_accept = true;

Outcome answer() { return g_accept ? Outcome{true, "ESP_OK"} : Outcome{false, "ESP_RTL_SDR_ERR_UNSUPPORTED"}; }

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.self_check = []() -> SelfCheck { g_calls.push_back("self_check"); return {true, "1.4.2", 3, 0x00ff10aau, 0x0000ff00u}; };
  hooks.installed = []() -> bool { g_calls.push_back("installed"); return g_installed; };
  hooks.offset_tuning = [](bool on) -> Outcome { g_calls.push_back(on ? "offset on" : "offset off"); return answer(); };
  hooks.gain_mode = [](bool automatic) -> Outcome { g_calls.push_back(automatic ? "gainmode auto" : "gainmode manual"); return answer(); };
  hooks.gain = [](int gain) -> Outcome { g_calls.push_back("gain " + std::to_string(gain)); return answer(); };
  hooks.rtl_agc = [](bool on) -> Outcome { g_calls.push_back(on ? "agc on" : "agc off"); return answer(); };
  hooks.bias_tee = [](bool on) -> Outcome { g_calls.push_back(on ? "bias on" : "bias off"); return answer(); };
  hooks.hf_direct = [](uint32_t hz) -> Outcome { g_calls.push_back("hfdirect " + std::to_string(hz)); return answer(); };
  hooks.bandwidth = [](uint32_t hz) -> Outcome { g_calls.push_back("bw " + std::to_string(hz)); return answer(); };
  hooks.retune = [](uint32_t hz) -> Outcome { g_calls.push_back("tune " + std::to_string(hz)); return answer(); };
  hooks.hf_direct_min_hz = 24000000u;
  hooks.hf_upconv_lo_hz = 28800000u;
  hooks.freq_min_hz = 24000u;
  hooks.freq_max_hz = 1766000000u;
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
  g_out.clear();
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

// One accepted action: the driver is asked first (after the installed check) and the reply echoes the action text.
void run_ok(const char* line, const char* action, const std::vector<std::string>& calls) {
  const Hooks hooks = make_hooks();
  CHECK(handle_command(line, true, hooks));
  std::vector<std::string> all{"installed"};
  all.insert(all.end(), calls.begin(), calls.end());
  expect_calls(all);
  expect_out({std::string("W:RTL_DRIVER_RESULT action=\"") + action + "\" accepted=1 result=ESP_OK\n"});
}

void run_invalid(const char* line, const char* reply) {
  const Hooks hooks = make_hooks();
  CHECK(handle_command(line, true, hooks));
  expect_calls({"installed"});
  expect_out({std::string("L:") + reply});
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // SELF_CHECK needs no authentication; one write, caps as 8 hex digits, profile as a decimal.
  CHECK(handle_command("RTL_DRIVER SELF_CHECK", false, hooks));
  expect_calls({"self_check"});
  expect_out({"W:RTL_DRIVER_SELF_CHECK pass=1 version=1.4.2 profile=3 device_caps=0x00ff10aa required=0x0000ff00\n"});

  // Every other RTL_DRIVER line is refused for an unauthenticated caller before any hook runs.
  for (const char* line : {"RTL_DRIVER OFFSET ON", "RTL_DRIVER TUNE 100000000", "RTL_DRIVER nonsense", "RTL_DRIVER ",
                           "RTL_DRIVER SELF_CHECK ", "RTL_DRIVER BW 0"}) {
    CHECK(handle_command(line, false, hooks));
    CHECK(g_calls.empty());
    expect_out({"L:RTL_DRIVER_ERROR auth_required"});
  }

  // No driver open: the check happens after authentication and before the action is parsed.
  g_installed = false;
  for (const char* line : {"RTL_DRIVER OFFSET ON", "RTL_DRIVER nonsense", "RTL_DRIVER ", "RTL_DRIVER TUNE x"}) {
    CHECK(handle_command(line, true, hooks));
    expect_calls({"installed"});
    expect_out({"L:RTL_DRIVER_ERROR not_installed"});
  }
  g_installed = true;

  // Each action reaches its hook with the parsed value and echoes the action text.
  run_ok("RTL_DRIVER OFFSET ON", "OFFSET ON", {"offset on"});
  run_ok("RTL_DRIVER OFFSET OFF", "OFFSET OFF", {"offset off"});
  run_ok("RTL_DRIVER GAINMODE AUTO", "GAINMODE AUTO", {"gainmode auto"});
  run_ok("RTL_DRIVER GAINMODE MANUAL", "GAINMODE MANUAL", {"gainmode manual"});
  run_ok("RTL_DRIVER GAIN 279", "GAIN 279", {"gain 279"});
  run_ok("RTL_DRIVER GAIN -10", "GAIN -10", {"gain -10"});
  run_ok("RTL_DRIVER GAIN 496", "GAIN 496", {"gain 496"});
  run_ok("RTL_DRIVER GAIN 0", "GAIN 0", {"gain 0"});
  run_ok("RTL_DRIVER RTLAGC ON", "RTLAGC ON", {"agc on"});
  run_ok("RTL_DRIVER RTLAGC OFF", "RTLAGC OFF", {"agc off"});
  run_ok("RTL_DRIVER BIAS ON", "BIAS ON", {"bias on"});
  run_ok("RTL_DRIVER BIAS OFF", "BIAS OFF", {"bias off"});
  run_ok("RTL_DRIVER HFDIRECT AUTO", "HFDIRECT AUTO", {"hfdirect 4294967295"});
  run_ok("RTL_DRIVER HFDIRECT OFF", "HFDIRECT OFF", {"hfdirect 0"});
  run_ok("RTL_DRIVER HFDIRECT 24000000", "HFDIRECT 24000000", {"hfdirect 24000000"});
  run_ok("RTL_DRIVER HFDIRECT 28799999", "HFDIRECT 28799999", {"hfdirect 28799999"});
  run_ok("RTL_DRIVER BW 0", "BW 0", {"bw 0"});
  run_ok("RTL_DRIVER BW 1500000", "BW 1500000", {"bw 1500000"});
  run_ok("RTL_DRIVER TUNE 24000", "TUNE 24000", {"tune 24000"});
  run_ok("RTL_DRIVER TUNE 162550000", "TUNE 162550000", {"tune 162550000"});
  run_ok("RTL_DRIVER TUNE 1766000000", "TUNE 1766000000", {"tune 1766000000"});

  // A refusal from the driver is reported as accepted=0 with the driver's own name for it.
  g_accept = false;
  CHECK(handle_command("RTL_DRIVER BIAS ON", true, hooks));
  expect_calls({"installed", "bias on"});
  expect_out({"W:RTL_DRIVER_RESULT action=\"BIAS ON\" accepted=0 result=ESP_RTL_SDR_ERR_UNSUPPORTED\n"});
  g_accept = true;

  // Range and parse errors reach no driver hook (only the installed check ran).
  run_invalid("RTL_DRIVER GAIN -11", "RTL_DRIVER_INVALID use GAIN <-10..496>; driver selects nearest supported step");
  run_invalid("RTL_DRIVER GAIN 497", "RTL_DRIVER_INVALID use GAIN <-10..496>; driver selects nearest supported step");
  run_invalid("RTL_DRIVER GAIN x", "RTL_DRIVER_INVALID use GAIN <-10..496>; driver selects nearest supported step");
  run_invalid("RTL_DRIVER GAIN ", "RTL_DRIVER_INVALID use GAIN <-10..496>; driver selects nearest supported step");
  run_invalid("RTL_DRIVER GAIN 5x", "RTL_DRIVER_INVALID use GAIN <-10..496>; driver selects nearest supported step");
  for (const char* bad : {"RTL_DRIVER HFDIRECT 23999999", "RTL_DRIVER HFDIRECT 28800000", "RTL_DRIVER HFDIRECT x",
                          "RTL_DRIVER HFDIRECT ", "RTL_DRIVER HFDIRECT auto", "RTL_DRIVER HFDIRECT 25000000x"}) {
    run_invalid(bad, "RTL_DRIVER_INVALID use HFDIRECT AUTO|OFF|<24000000..28799999>");
  }
  run_invalid("RTL_DRIVER BW x", "RTL_DRIVER_INVALID use BW <hz|0>");
  run_invalid("RTL_DRIVER BW ", "RTL_DRIVER_INVALID use BW <hz|0>");
  run_invalid("RTL_DRIVER BW 10k", "RTL_DRIVER_INVALID use BW <hz|0>");
  for (const char* bad : {"RTL_DRIVER TUNE 23999", "RTL_DRIVER TUNE 1766000001", "RTL_DRIVER TUNE x", "RTL_DRIVER TUNE ",
                          "RTL_DRIVER TUNE 100M", "RTL_DRIVER TUNE 99999999999999999999"}) {
    run_invalid(bad, "RTL_DRIVER_INVALID use TUNE <24000..1766000000>");
  }

  // Anything else after the prefix lists the usage (exact words, case sensitive, no trailing text).
  for (const char* bad : {"RTL_DRIVER ", "RTL_DRIVER STATUS", "RTL_DRIVER OFFSET", "RTL_DRIVER OFFSET ON ", "RTL_DRIVER offset on",
                          "RTL_DRIVER GAINMODE", "RTL_DRIVER GAINMODE AUTO ", "RTL_DRIVER GAINMODE FOO", "RTL_DRIVER GAINX",
                          "RTL_DRIVER RTLAGC", "RTL_DRIVER RTLAGC MAYBE", "RTL_DRIVER BIAS", "RTL_DRIVER BIAS ON ", "RTL_DRIVER HFDIRECT",
                          "RTL_DRIVER BW", "RTL_DRIVER TUNE", "RTL_DRIVER SELF_CHECK "}) {
    run_invalid(bad,
                "RTL_DRIVER_INVALID use STATUS|SELF_CHECK|TUNE <HZ>|OFFSET ON|OFF|HFDIRECT AUTO|OFF|<24000000..28799999>|GAINMODE AUTO|MANUAL|GAIN <-10..496>|RTLAGC ON|OFF|BIAS ON|OFF|BW <hz|0>");
  }

  // Lines that are not ours fall through to the dispatcher (RTL_DRIVER STATUS has its own route earlier).
  for (const char* other : {"RTL_DRIVER", "RTL_DRIVER_STATUS", "RTL_DRIVERS", "RTL_DRIVER_SELF_CHECK", "rtl_driver self_check", ""}) {
    CHECK(!handle_command(other, true, hooks));
  }
  CHECK(g_out.empty() && g_calls.empty());

  reset();
  if (failures == 0) std::printf("PASS: RTL_DRIVER SELF_CHECK and action replies, refusals, ranges and call order\n");
  return failures == 0 ? 0 : 1;
}

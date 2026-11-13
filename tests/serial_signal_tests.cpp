// Host test for the RTL_SIGNAL / RTL_PRESET_SCAN command family (serial_signal.cpp). The expected replies are the
// strings the dispatcher in main.cpp used before the family moved, filled in with the scripted figures below.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_signal_tests.cpp and apps/orcsdr-tab5/ui/serial_signal.cpp
#include "serial_signal.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_signal;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_out;
std::vector<std::string> g_calls;
bool g_fm = true;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.signal = []() -> Signal {
    g_calls.push_back("signal");
    Signal s;
    s.band = "FM";
    s.frequency_hz = 98700000u;
    s.signal_tenths = -452;
    s.stereo_locked = true;
    s.left_tenths = -300;
    s.right_tenths = -310;
    s.rds_carrier = true;
    s.rds_tenths = -655;
    s.pilot_env_thou = 87;
    s.clipping_percent = 0.5f;
    s.filter_hz = 200000u;
    return s;
  };
  hooks.band_is_fm = []() -> bool {
    g_calls.push_back("band_is_fm");
    return g_fm;
  };
  hooks.request_preset_scan = []() { g_calls.push_back("scan"); };
  return hooks;
}

void reset() {
  g_out.clear();
  g_calls.clear();
  g_fm = true;
}

bool run(const char* line, bool authenticated) {
  const Hooks hooks = make_hooks();
  return handle_command(line, authenticated, hooks);
}

}  // namespace

int main() {
  reset();
  CHECK(run("RTL_SIGNAL", false));   // no authentication needed
  CHECK(g_calls.size() == 1 && g_calls[0] == "signal");
  CHECK(g_out.size() == 1 &&
        g_out[0] ==
            "W:RTL_SIGNAL_STATUS band=FM frequency_hz=98700000 signal_dbfs_tenths=-452 stereo_locked=1 "
            "left_dbfs_tenths=-300 right_dbfs_tenths=-310 rds_carrier=1 rds_signal_tenths=-655 pilot_env_thou=87 "
            "clipping_percent=0.500 filter_hz=200000 lo_nudge=0\n");

  reset();
  CHECK(run("RTL_PRESET_SCAN", true));
  CHECK(g_calls.size() == 2 && g_calls[0] == "band_is_fm" && g_calls[1] == "scan");
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_PRESET_SCAN_QUEUED");

  reset();
  g_fm = false;
  CHECK(run("RTL_PRESET_SCAN", true));
  CHECK(g_calls.size() == 1 && g_calls[0] == "band_is_fm");
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_PRESET_SCAN_INVALID FM band only");

  // Not authenticated: not ours, nothing called.
  reset();
  CHECK(!run("RTL_PRESET_SCAN", false));
  CHECK(g_calls.empty() && g_out.empty());

  // Exactness.
  CHECK(!run("RTL_SIGNAL ", true));
  CHECK(!run("RTL_SIGNALS", true));
  CHECK(!run("RTL_PRESET_SCAN ", true));
  CHECK(!run("RTL_PRESET_SCAN_STOP", true));
  CHECK(!run("rtl_signal", true));
  CHECK(!run("", true));
  CHECK(g_calls.empty() && g_out.empty());

  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("serial_signal tests passed\n");
  return 0;
}

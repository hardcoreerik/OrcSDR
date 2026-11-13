// Host test for the read-only status command family (serial_status.cpp). The expected replies are the format strings
// the dispatcher in main.cpp used before the family moved, filled in with the scripted facts below. Every value here
// is made up.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_status_tests.cpp and apps/orcsdr-tab5/ui/serial_status.cpp
#include "serial_status.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_status;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write, "L:" a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.rtl_status = []() -> RtlStatus {
    g_calls.push_back("rtl_status");
    RtlStatus s;
    s.connected = true;
    s.vid = 0x0bda;
    s.pid = 0x2838;
    s.speed = "high";
    s.serial = "TEST0001";
    return s;
  };
  hooks.hotplug_status = []() -> HotplugStatus {
    g_calls.push_back("hotplug");
    HotplugStatus h;
    h.pending = 1;
    h.ready = 0;
    h.state = "streaming";
    h.band = "fm";
    h.frequency_hz = 98100000u;
    return h;
  };
  hooks.print_adsb_status = [] { g_calls.push_back("adsb"); };
  hooks.codec_register = [](uint8_t reg) -> uint8_t {
    g_calls.push_back("reg" + std::to_string(reg));
    return static_cast<uint8_t>(reg + 0x80 > 0xff ? 0x0a : reg + 0x80);
  };
  hooks.audio_state = []() -> AudioState {
    g_calls.push_back("audio");
    return {1, 0, 1, 0, 37};
  };
  hooks.print_driver_status = [] { g_calls.push_back("driver"); };
  hooks.am_scan_status = []() -> AmScanStatus {
    g_calls.push_back("am_scan");
    return {1, 12, 120, 3, 1234000ul, 0};
  };
  hooks.self_check = []() -> SelfCheck {
    g_calls.push_back("self_check");
    return {1, 1, 0, 1, 0, 1};
  };
  hooks.home_plot_status = [](char* out, size_t size) {
    g_calls.push_back("home_plot");
    std::snprintf(out, size, "HOME_PLOT_STATUS fake=1");
  };
  return hooks;
}

void reset() {
  g_out.clear();
  g_calls.clear();
}

bool run(const char* command, bool authenticated) {
  reset();
  const Hooks hooks = make_hooks();
  return handle_command(command, authenticated, hooks);
}

void expect(const std::vector<std::string>& lines) {
  if (g_out != lines) {
    std::printf("FAIL: output differs\n  got:");
    for (const auto& line : g_out) std::printf(" [%s]", line.c_str());
    std::printf("\n  want:");
    for (const auto& line : lines) std::printf(" [%s]", line.c_str());
    std::printf("\n");
    ++failures;
  }
}

void test_rtl_and_hotplug() {
  for (const bool auth : {false, true}) {
    CHECK(run("RTL_STATUS", auth));
    expect({"W:RTL_SDR_STATUS connected=true vid=0bda pid=2838 speed=high serial=\"TEST0001\"\n"});
    CHECK(g_calls == std::vector<std::string>{"rtl_status"});
    CHECK(run("RTL_HOTPLUG_STATUS", auth));
    expect({"W:RTL_HOTPLUG_STATUS pending=1 ready=0 state=streaming band=fm frequency_hz=98100000\n"});
  }
}

void test_rtl_disconnected_format() {
  reset();
  Hooks hooks = make_hooks();
  hooks.rtl_status = []() -> RtlStatus { return RtlStatus{}; };
  CHECK(handle_command("RTL_STATUS", false, hooks));
  expect({"W:RTL_SDR_STATUS connected=false vid=0000 pid=0000 speed= serial=\"\"\n"});
}

void test_delegated() {
  CHECK(run("RTL_ADSB STATUS", false));
  CHECK(g_out.empty());
  CHECK(g_calls == std::vector<std::string>{"adsb"});
  CHECK(run("RTL_DRIVER STATUS", false));
  CHECK(g_out.empty());
  CHECK(g_calls == std::vector<std::string>{"driver"});
}

void test_audio_codec() {
  CHECK(run("AUDIO_CODEC", false));
  expect({"W:AUDIO_CODEC", "W: r2=0x82", "W: r4=0x84", "W: r25=0x99", "W: r26=0x9a", "W: r27=0x9b",
          "W: r39=0xa7", "W: r42=0xaa", "W: r46=0xae", "W: r47=0xaf", "W: r48=0xb0", "W: r49=0xb1",
          "W: hp_detect=1 amp_pin=0 muted=1 running=0 volume=37\n"});
  CHECK(g_calls == (std::vector<std::string>{"reg2", "reg4", "reg25", "reg26", "reg27", "reg39", "reg42", "reg46",
                                              "reg47", "reg48", "reg49", "audio"}));
}

void test_am_scan_and_self_check() {
  CHECK(run("RTL_AM_SCAN STATUS", false));
  expect({"W:RTL_AM_SCAN_STATUS active=1 step=12 total=120 found=3 frequency_hz=1234000 prompt=0\n"});
  CHECK(run("UI_SELF_CHECK", false));
  expect({"W:UI_SELF_CHECK am=1 fm=1 home=0 shortwave=1 keypad=0 editor=1\n"});
}

void test_home_plot() {
  CHECK(run("HOME_PLOT STATUS", false));
  expect({"L:HOME_PLOT_STATUS fake=1"});
  reset();
  Hooks hooks = make_hooks();
  hooks.home_plot_status = [](char* out, size_t size) {
    CHECK(size == 384);
    out[0] = 0;
  };
  CHECK(handle_command("HOME_PLOT STATUS", false, hooks));
  expect({"L:"});
}

void test_not_ours() {
  const char* const lines[] = {"",
                               "RTL_STATUS ",
                               "rtl_status",
                               "RTL_STATUS_",
                               "RTL_HOTPLUG_STATUS ",
                               "RTL_HOTPLUG",
                               "RTL_ADSB STATUS ",
                               "RTL_ADSB",
                               "RTL_ADSB  STATUS",
                               "AUDIO_CODEC ",
                               "AUDIO_CODEC 1",
                               "RTL_DRIVER STATUS ",
                               "RTL_DRIVER SELF_CHECK",
                               "RTL_AM_SCAN STATUS ",
                               "RTL_AM_SCAN",
                               "UI_SELF_CHECK ",
                               "UI_SELF_CHECK 1",
                               "HOME_PLOT STATUS ",
                               "HOME_PLOT",
                               "RTL_DSP LAB",
                               "UI_TAP 1 2"};
  for (const char* line : lines) {
    CHECK(!run(line, true));
    CHECK(g_out.empty());
    CHECK(g_calls.empty());
  }
}

}  // namespace

int main() {
  test_rtl_and_hotplug();
  test_rtl_disconnected_format();
  test_delegated();
  test_audio_codec();
  test_am_scan_and_self_check();
  test_home_plot();
  test_not_ours();
  if (failures == 0) std::printf("serial_status_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}

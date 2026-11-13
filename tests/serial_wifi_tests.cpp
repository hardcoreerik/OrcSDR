// Host test for the RTL_WIFI command family (serial_wifi.cpp). The expected replies are the format strings the
// dispatcher in main.cpp used before the family moved, filled in with the scripted facts below. Every SSID, BSSID
// and number here is made up.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui, twice: as is and
// with -DORCSDR_C6_FAULT_TEST=1 (the lab power route exists only in that build):
// tests/serial_wifi_tests.cpp and apps/orcsdr-tab5/ui/serial_wifi.cpp
#include "serial_wifi.hpp"

#include <cstdio>
#include <string>
#include <vector>

#ifndef ORCSDR_C6_FAULT_TEST
#define ORCSDR_C6_FAULT_TEST 0
#endif

using namespace orcsdr::serial_wifi;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// Every write is recorded separately, so the test also pins how the reply is split into writes:
// "W:" is one verbatim write, "L:" a line written through println, "H:" one hex dump (the device sends it one
// character per write).
std::vector<std::string> g_out;
std::vector<std::string> g_calls;

// Scripted state.
uint8_t g_profile_count = 2;
uint8_t g_result_count = 2;
bool g_disconnect_ok = true;
bool g_c6_power_ok = true;
bool g_c6_update_ok = true;
bool g_link_available = true;
bool g_channel_ok = true;
bool g_channel_has_ap = true;
std::string g_stage = "download";
unsigned long g_outage_left = 0;
uint32_t g_outage_set = 0xffffffffu;
int g_count_calls = 0;

const uint8_t kBssidA[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0xab};
const uint8_t kBssidB[6] = {0xfe, 0x00, 0x01, 0x02, 0x03, 0x04};
const char* const kResultSsid[2] = {"TestNet", "Lab-2"};
const char* const kProfileSsid[4] = {"HomeA", "HomeB", "", ""};

std::string hex(const uint8_t* bytes, size_t size) {
  static const char kHex[] = "0123456789abcdef";
  std::string text;
  for (size_t i = 0; i < size; ++i) {
    text += kHex[bytes[i] >> 4];
    text += kHex[bytes[i] & 0x0f];
  }
  return text;
}

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.write_hex = [](const uint8_t* bytes, size_t size) { g_out.push_back("H:" + hex(bytes, size)); };
  hooks.request_scan = [] { g_calls.push_back("scan"); };
  hooks.profile_count = []() -> uint8_t { return g_profile_count; };
  hooks.connect_saved_profile = [] { g_calls.push_back("connect"); };
  hooks.scan_summary = []() -> ScanSummary { return {static_cast<unsigned>(g_result_count), 7, 12, 34, 5600}; };
  hooks.scan_result_count = []() -> uint8_t { ++g_count_calls; return g_result_count; };
  hooks.scan_result = [](uint8_t index) -> ScanResult {
    ScanResult ap;
    ap.ssid = kResultSsid[index];
    ap.bssid = index == 0 ? kBssidA : kBssidB;
    ap.rssi = index == 0 ? -61 : -90;
    ap.channel = index == 0 ? 6 : 11;
    ap.secure = index == 0 ? 1 : 0;
    ap.security = index == 0 ? "WPA2-PSK" : "OPEN";
    ap.phy = index == 0 ? "11bgn" : "11b";
    ap.ht40 = index == 0 ? 1 : 0;
    return ap;
  };
  hooks.profile_ssid = [](uint8_t index) -> const char* { return kProfileSsid[index]; };
  hooks.profile_connected = [](uint8_t index) -> bool { return index == 1; };
  hooks.simulate_outage = [](uint32_t seconds) { g_calls.push_back("outage"); g_outage_set = seconds; g_outage_left = seconds; };
  hooks.outage_remaining_s = []() -> unsigned long { return g_outage_left; };
  hooks.disconnect = []() -> bool { g_calls.push_back("disconnect"); return g_disconnect_ok; };
  hooks.set_c6_power = [](bool enable) -> bool { g_calls.push_back(enable ? "c6_on" : "c6_off"); return g_c6_power_ok; };
  hooks.bus_status = []() -> BusStatus { g_calls.push_back("bus"); return {1, 0, 1, 0, 3}; };
  hooks.status = []() -> Status { return {1, 1, 0, 0, 1, 2, 1, 0, "external", 9, -58, 4}; };
  hooks.link = [](Link* out) -> bool {
    if (!g_link_available) return false;
    *out = {-58, 6, 1, 1, 1, 1, 0, 2, 1};
    return true;
  };
  hooks.channel = []() -> Channel {
    Channel c;
    if (!g_channel_ok) {
      c.ok = false;
      c.error = "ESP_ERR_WIFI_NOT_INIT";
      return c;
    }
    c.primary = 6;
    c.secondary = 1;
    c.connected = g_channel_has_ap ? 1 : 0;
    c.has_ap = g_channel_has_ap;
    c.ssid[0] = 'A';
    c.ssid[1] = 'p';
    c.ssid[2] = 0xff;
    c.ssid_length = 3;
    c.ap_primary = 6;
    return c;
  };
  hooks.c6_status = []() -> C6Status {
    C6Status c6{"3.0.6", 1, 1, "downloading", 42, {}, 1};
    std::snprintf(c6.stage, sizeof(c6.stage), "%s", g_stage.c_str());
    return c6;
  };
  hooks.begin_c6_update = []() -> bool { g_calls.push_back("c6_update"); return g_c6_update_ok; };
  hooks.print_coexistence = [] { g_calls.push_back("coex"); g_out.push_back("W:RTL_WIFI_COEX_STATUS fake\n"); };
  return hooks;
}

void reset() {
  g_out.clear();
  g_calls.clear();
  g_profile_count = 2;
  g_result_count = 2;
  g_disconnect_ok = true;
  g_c6_power_ok = true;
  g_c6_update_ok = true;
  g_link_available = true;
  g_channel_ok = true;
  g_channel_has_ap = true;
  g_stage = "download";
  g_outage_left = 0;
  g_outage_set = 0xffffffffu;
  g_count_calls = 0;
}

void expect(const std::vector<std::string>& expected) {
  if (g_out != expected) {
    ++failures;
    std::printf("FAIL output:\n");
    for (size_t i = 0; i < g_out.size() || i < expected.size(); ++i) {
      std::printf("  got:  %s\n  want: %s\n", i < g_out.size() ? g_out[i].c_str() : "<nothing>",
                  i < expected.size() ? expected[i].c_str() : "<nothing>");
    }
  }
}

// Runs one command; returns whether the family claimed it.
bool run(const char* command, bool authenticated) {
  reset();
  return handle_command(command, authenticated, make_hooks());
}

bool run_keep(const char* command, bool authenticated) {   // does not reset the scripted state
  g_out.clear();
  g_calls.clear();
  return handle_command(command, authenticated, make_hooks());
}

void test_scan_and_connect() {
  CHECK(run("RTL_WIFI_SCAN", false));
  expect({"L:RTL_WIFI_SCAN_QUEUED"});
  CHECK(g_calls == std::vector<std::string>{"scan"});
  CHECK(!run("RTL_WIFI_SCAN ", false));
  CHECK(!run("RTL_WIFI_SCANX", false));
  CHECK(!run("rtl_wifi_scan", false));

  CHECK(run("RTL_WIFI_CONNECT_SAVED", false));
  expect({"L:RTL_WIFI_CONNECT_QUEUED saved_profile=0 mode=pause"});
  CHECK(g_calls == std::vector<std::string>{"connect"});
  CHECK(run("RTL_WIFI_CONNECT_SAVED PAUSE", false));
  expect({"L:RTL_WIFI_CONNECT_QUEUED saved_profile=0 mode=pause"});
  CHECK(!run("RTL_WIFI_CONNECT_SAVED PAUSE ", false));
  CHECK(!run("RTL_WIFI_CONNECT_SAVED  PAUSE", false));
  CHECK(!run("RTL_WIFI_CONNECT_SAVED RESUME", false));
  g_profile_count = 0;
  CHECK(run_keep("RTL_WIFI_CONNECT_SAVED", false));
  expect({"L:RTL_WIFI_CONNECT_ERROR no_saved_profile"});
  CHECK(g_calls.empty());
  CHECK(run_keep("RTL_WIFI_CONNECT_SAVED PAUSE", false));
  expect({"L:RTL_WIFI_CONNECT_ERROR no_saved_profile"});
}

void test_results_and_profiles() {
  CHECK(run("RTL_WIFI_RESULTS", false));
  expect({"W:RTL_WIFI_RESULTS_BEGIN count=2 total=7 revision=12 age_s=34 duration_ms=5600\n",
          "W:RTL_WIFI_AP index=0 ssid_hex=", "H:" + hex(reinterpret_cast<const uint8_t*>("TestNet"), 7), "W: bssid=",
          "H:0211223344ab",
          "W: rssi=-61 channel=6 secure=1 security=WPA2-PSK phy=11bgn ht40=1\n",
          "W:RTL_WIFI_AP index=1 ssid_hex=", "H:" + hex(reinterpret_cast<const uint8_t*>("Lab-2"), 5), "W: bssid=",
          "H:fe0001020304", "W: rssi=-90 channel=11 secure=0 security=OPEN phy=11b ht40=0\n",
          "L:RTL_WIFI_RESULTS_END"});
  CHECK(g_count_calls == 3);   // the loop bound is read again for every pass, as the original did
  CHECK(!run("RTL_WIFI_RESULTS ", false));
  g_result_count = 0;
  CHECK(run_keep("RTL_WIFI_RESULTS", false));
  expect({"W:RTL_WIFI_RESULTS_BEGIN count=0 total=7 revision=12 age_s=34 duration_ms=5600\n",
          "L:RTL_WIFI_RESULTS_END"});

  CHECK(run("RTL_WIFI_PROFILES", false));
  expect({"W:RTL_WIFI_PROFILES_BEGIN count=2\n", "W:RTL_WIFI_PROFILE index=0 ssid_hex=",
          "H:" + hex(reinterpret_cast<const uint8_t*>("HomeA"), 5), "W: connected=0\n",
          "W:RTL_WIFI_PROFILE index=1 ssid_hex=", "H:" + hex(reinterpret_cast<const uint8_t*>("HomeB"), 5),
          "W: connected=1\n", "L:RTL_WIFI_PROFILES_END"});
  CHECK(!run("RTL_WIFI_PROFILES ", false));
  g_profile_count = 0;
  CHECK(run_keep("RTL_WIFI_PROFILES", false));
  expect({"W:RTL_WIFI_PROFILES_BEGIN count=0\n", "L:RTL_WIFI_PROFILES_END"});
}

void test_outage() {
  CHECK(run("RTL_WIFI_OUTAGE", false));
  expect({"L:RTL_WIFI_OUTAGE_ERROR auth_required"});
  CHECK(g_calls.empty());
  CHECK(run("RTL_WIFI_OUTAGE 30", false));
  expect({"L:RTL_WIFI_OUTAGE_ERROR auth_required"});
  CHECK(g_calls.empty());
  CHECK(!run("RTL_WIFI_OUTAGEX", true));
  CHECK(!run("RTL_WIFI_OUTAGE30", true));

  // No argument: report only.
  g_outage_left = 12;
  CHECK(run_keep("RTL_WIFI_OUTAGE", true));
  expect({"W:RTL_WIFI_OUTAGE seconds_left=12\n"});
  CHECK(g_calls.empty());
  CHECK(run("RTL_WIFI_OUTAGE 30", true));
  expect({"W:RTL_WIFI_OUTAGE seconds_left=30\n"});
  CHECK(g_outage_set == 30);
  CHECK(run("RTL_WIFI_OUTAGE 1800", true));
  expect({"W:RTL_WIFI_OUTAGE seconds_left=1800\n"});
  CHECK(run("RTL_WIFI_OUTAGE 0", true));
  expect({"W:RTL_WIFI_OUTAGE seconds_left=0\n"});
  CHECK(g_outage_set == 0);
  CHECK(run("RTL_WIFI_OUTAGE 1801", true));
  expect({"L:RTL_WIFI_OUTAGE_ERROR use 0 (cancel) to 1800 seconds"});
  CHECK(g_calls.empty());
  // A trailing space parses as zero seconds and still reaches the hook, as strtoul did.
  CHECK(run("RTL_WIFI_OUTAGE ", true));
  expect({"W:RTL_WIFI_OUTAGE seconds_left=0\n"});
  CHECK(g_outage_set == 0);
  CHECK(run("RTL_WIFI_OUTAGE abc", true));
  expect({"W:RTL_WIFI_OUTAGE seconds_left=0\n"});
}

void test_disconnect_and_c6_power() {
  CHECK(run("RTL_WIFI_DISCONNECT", false));
  expect({"L:RTL_WIFI_DISCONNECT_ERROR auth_required"});
  CHECK(g_calls.empty());
  CHECK(run("RTL_WIFI_DISCONNECT", true));
  expect({"L:RTL_WIFI_DISCONNECT_OK"});
  g_disconnect_ok = false;
  CHECK(run_keep("RTL_WIFI_DISCONNECT", true));
  expect({"L:RTL_WIFI_DISCONNECT_ERROR radio_pause_failed"});
  CHECK(!run("RTL_WIFI_DISCONNECT ", true));

#if ORCSDR_C6_FAULT_TEST
  CHECK(run("RTL_WIFI_TEST_C6_POWER OFF CONFIRM", false));
  expect({"L:RTL_WIFI_TEST_C6_POWER_ERROR auth_required"});
  CHECK(g_calls.empty());
  CHECK(run("RTL_WIFI_TEST_C6_POWER ON CONFIRM", false));
  expect({"L:RTL_WIFI_TEST_C6_POWER_ERROR auth_required"});
  CHECK(run("RTL_WIFI_TEST_C6_POWER OFF CONFIRM", true));
  expect({"W:RTL_WIFI_TEST_C6_POWER_RESULT enabled=0 write_ok=1\n"});
  CHECK(g_calls == std::vector<std::string>{"c6_off"});
  CHECK(run("RTL_WIFI_TEST_C6_POWER ON CONFIRM", true));
  expect({"W:RTL_WIFI_TEST_C6_POWER_RESULT enabled=1 write_ok=1\n"});
  CHECK(g_calls == std::vector<std::string>{"c6_on"});
  g_c6_power_ok = false;
  CHECK(run_keep("RTL_WIFI_TEST_C6_POWER ON CONFIRM", true));
  expect({"W:RTL_WIFI_TEST_C6_POWER_RESULT enabled=1 write_ok=0\n"});
  CHECK(!run("RTL_WIFI_TEST_C6_POWER ON", true));
  CHECK(!run("RTL_WIFI_TEST_C6_POWER OFF CONFIRM ", true));
  CHECK(!run("RTL_WIFI_TEST_C6_POWER CONFIRM", true));
#else
  // Without the lab flag the route does not exist at all, even for an authenticated session.
  CHECK(!run("RTL_WIFI_TEST_C6_POWER OFF CONFIRM", true));
  CHECK(!run("RTL_WIFI_TEST_C6_POWER ON CONFIRM", true));
  CHECK(!run("RTL_WIFI_TEST_C6_POWER ON CONFIRM", false));
  CHECK(g_calls.empty());
#endif
}

void test_status_family() {
  CHECK(run("RTL_WIFI_BUS_STATUS", false));
  expect({"W:RTL_WIFI_BUS_STATUS wlan_enable=1 wlan_input=0 hosted_ready=1 link_failed=0 capture_state=3\n"});
  CHECK(g_calls == std::vector<std::string>{"bus"});
  CHECK(!run("RTL_WIFI_BUS_STATUS ", false));

  CHECK(run("RTL_WIFI_STATUS", false));
  expect({"W:RTL_WIFI_STATUS station=1 hosted_match=1 scanning=0 connecting=0 connected=1 saved_profiles=2 power=1 "
          "auto_connect=0 antenna=external ap_count=9 rssi=-58 outage_s=4\n",
          "W:RTL_WIFI_LINK rssi=-58 channel=6 second=1 b=1 g=1 n=1 ax=0 ps=2 bandwidth=1\n"});
  g_link_available = false;
  CHECK(run_keep("RTL_WIFI_STATUS", false));
  expect({"W:RTL_WIFI_STATUS station=1 hosted_match=1 scanning=0 connecting=0 connected=1 saved_profiles=2 power=1 "
          "auto_connect=0 antenna=external ap_count=9 rssi=-58 outage_s=4\n"});
  CHECK(!run("RTL_WIFI_STATUS ", false));

  CHECK(run("RTL_WIFI_CHANNEL", false));
  expect({"W:RTL_WIFI_CHANNEL primary=6 secondary=1 connected=1 ssid_hex=", "H:4170ff", "W: ap_primary=6\n"});
  g_channel_has_ap = false;
  CHECK(run_keep("RTL_WIFI_CHANNEL", false));
  expect({"W:RTL_WIFI_CHANNEL primary=6 secondary=1 connected=0 ssid_hex=", "W: ap_primary=0\n"});
  g_channel_ok = false;
  CHECK(run_keep("RTL_WIFI_CHANNEL", false));
  expect({"W:RTL_WIFI_CHANNEL_ERROR ESP_ERR_WIFI_NOT_INIT\n"});
  CHECK(!run("RTL_WIFI_CHANNEL ", false));

  CHECK(run("RTL_WIFI_C6_STATUS", false));
  expect({"W:RTL_WIFI_C6_STATUS host=3.0.6 coprocessor=3.0.6 transport=1 embedded=1 state=downloading percent=42 "
          "stage=download match=1\n"});
  g_stage = "";
  CHECK(run_keep("RTL_WIFI_C6_STATUS", false));
  expect({"W:RTL_WIFI_C6_STATUS host=3.0.6 coprocessor=3.0.6 transport=1 embedded=1 state=downloading percent=42 "
          "stage=none match=1\n"});
  CHECK(!run("RTL_WIFI_C6_STATUS ", false));

  CHECK(run("RTL_WIFI_COEX_STATUS", false));
  expect({"W:RTL_WIFI_COEX_STATUS fake\n"});
  CHECK(g_calls == std::vector<std::string>{"coex"});
  CHECK(!run("RTL_WIFI_COEX_STATUS ", false));
}

void test_c6_update() {
  CHECK(run("RTL_WIFI_C6_UPDATE CONFIRM", false));
  expect({"L:RTL_WIFI_C6_UPDATE_ERROR auth_required"});
  CHECK(g_calls.empty());
  CHECK(run("RTL_WIFI_C6_UPDATE CONFIRM", true));
  expect({"L:RTL_WIFI_C6_UPDATE_QUEUED"});
  CHECK(g_calls == std::vector<std::string>{"c6_update"});
  g_c6_update_ok = false;
  CHECK(run_keep("RTL_WIFI_C6_UPDATE CONFIRM", true));
  expect({"L:RTL_WIFI_C6_UPDATE_ERROR not_ready"});
  CHECK(!run("RTL_WIFI_C6_UPDATE", true));
  CHECK(!run("RTL_WIFI_C6_UPDATE CONFIRM ", true));
  CHECK(!run("RTL_WIFI_C6_UPDATE confirm", true));
  CHECK(g_calls.empty());
}

void test_not_ours() {
  CHECK(!run("", true));
  CHECK(!run("RTL_WIFI_C6_POWER OFF", true));   // a separate route, later in the dispatcher
  CHECK(!run("RTL_WIFI", true));
  CHECK(!run("UI_TAP 1 2", true));
  CHECK(g_out.empty());
  CHECK(g_calls.empty());
}

}  // namespace

int main() {
  test_scan_and_connect();
  test_results_and_profiles();
  test_outage();
  test_disconnect_and_c6_power();
  test_status_family();
  test_c6_update();
  test_not_ours();
  if (failures == 0) std::printf("serial_wifi_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}

// Host test for the RTL_ADSB range / location / start / stop command family (serial_adsb.cpp). The expected replies are
// the strings the dispatcher in main.cpp used before the family moved. Coordinates are made up.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_adsb_tests.cpp and apps/orcsdr-tab5/ui/serial_adsb.cpp
#include "serial_adsb.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_adsb;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write (it carries its own '\n'), "L:" is a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;
bool g_save_ok = true;
bool g_band_adsb = true;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.range_status = []() -> RangeStatus {
    g_calls.push_back("range_status");
    return {25, 50, true, false};
  };
  hooks.set_range = [](uint16_t range) { g_calls.push_back("set_range " + std::to_string(range)); };
  hooks.save_location = [](int32_t latitude_e7, int32_t longitude_e7) -> bool {
    g_calls.push_back("save " + std::to_string(latitude_e7) + " " + std::to_string(longitude_e7));
    return g_save_ok;
  };
  hooks.refresh_if_active = [] { g_calls.push_back("refresh"); };
  hooks.adsb_band_selected = []() -> bool { g_calls.push_back("band"); return g_band_adsb; };
  hooks.request_stop = [] { g_calls.push_back("stop"); };
  hooks.start = [] { g_calls.push_back("start"); };
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
  g_save_ok = true;
  g_band_adsb = true;
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // RTL_ADSB_RANGE needs no authentication.
  reset();
  CHECK(handle_command("RTL_ADSB_RANGE", false, hooks));
  expect({"W:RTL_ADSB_RANGE_STATUS range_nm=25 dashboard_range_nm=50 active=1 pending=0\n"});
  CHECK(g_calls.size() == 1 && g_calls[0] == "range_status");

  // RTL_ADSB_RANGE <n> refuses an unauthenticated caller before parsing or doing anything.
  for (const char* command : {"RTL_ADSB_RANGE 50", "RTL_ADSB_RANGE ", "RTL_ADSB_RANGE junk"}) {
    reset();
    CHECK(handle_command(command, false, hooks));
    CHECK(g_calls.empty());
    expect({"L:RTL_ADSB_RANGE_ERROR auth_required"});
  }
  // Only the four dashboard ranges are accepted, as exact text.
  for (const char* bad : {"RTL_ADSB_RANGE ", "RTL_ADSB_RANGE 0", "RTL_ADSB_RANGE 11", "RTL_ADSB_RANGE 200", "RTL_ADSB_RANGE 010",
                          "RTL_ADSB_RANGE 50 ", "RTL_ADSB_RANGE  50", "RTL_ADSB_RANGE 50nm", "RTL_ADSB_RANGE -10"}) {
    reset();
    CHECK(handle_command(bad, true, hooks));
    CHECK(g_calls.empty());
    expect({"L:RTL_ADSB_RANGE_ERROR invalid_range"});
  }
  const struct { const char* command; const char* range; } ranges[] = {
      {"RTL_ADSB_RANGE 10", "10"}, {"RTL_ADSB_RANGE 25", "25"}, {"RTL_ADSB_RANGE 50", "50"}, {"RTL_ADSB_RANGE 100", "100"}};
  for (const auto& entry : ranges) {
    reset();
    CHECK(handle_command(entry.command, true, hooks));
    CHECK(g_calls.size() == 1 && g_calls[0] == std::string("set_range ") + entry.range);
    expect({std::string("W:RTL_ADSB_RANGE_OK range_nm=") + entry.range + " persistence=pending\n"});
  }

  // RTL_ADSB_LOCATION refuses an unauthenticated caller before parsing.
  for (const char* command : {"RTL_ADSB_LOCATION 12.5 -45.5", "RTL_ADSB_LOCATION "}) {
    reset();
    CHECK(handle_command(command, false, hooks));
    CHECK(g_calls.empty());
    expect({"L:RTL_ADSB_LOCATION_ERROR auth_required"});
  }
  // Wrong shapes and out-of-range values are refused without touching storage.
  for (const char* bad : {"RTL_ADSB_LOCATION ", "RTL_ADSB_LOCATION 12.5", "RTL_ADSB_LOCATION 12.5 -45.5 extra",
                          "RTL_ADSB_LOCATION north east", "RTL_ADSB_LOCATION 90.0001 10", "RTL_ADSB_LOCATION -90.0001 10",
                          "RTL_ADSB_LOCATION 10 180.0001", "RTL_ADSB_LOCATION 10 -180.0001", "RTL_ADSB_LOCATION nan 10",
                          "RTL_ADSB_LOCATION 10 inf", "RTL_ADSB_LOCATION 1e999 10"}) {
    reset();
    CHECK(handle_command(bad, true, hooks));
    CHECK(g_calls.empty());
    expect({"L:RTL_ADSB_LOCATION_ERROR invalid_coordinate"});
  }
  // A good position is converted to 1e-7 degrees (rounded), saved, then the dashboard is refreshed, then OK.
  reset();
  CHECK(handle_command("RTL_ADSB_LOCATION 12.3456789 -45.6789012", true, hooks));
  CHECK(g_calls.size() == 2 && g_calls[0] == "save 123456789 -456789012" && g_calls[1] == "refresh");
  expect({"L:RTL_ADSB_LOCATION_OK"});
  // The limits themselves are valid; extra spaces between the numbers are fine.
  reset();
  CHECK(handle_command("RTL_ADSB_LOCATION -90 180", true, hooks));
  CHECK(g_calls.size() == 2 && g_calls[0] == "save -900000000 1800000000");
  expect({"L:RTL_ADSB_LOCATION_OK"});
  reset();
  CHECK(handle_command("RTL_ADSB_LOCATION   0.00000004    0.00000006", true, hooks));
  CHECK(g_calls[0] == "save 0 1");   // rounds to the nearest 1e-7
  expect({"L:RTL_ADSB_LOCATION_OK"});
  // A refused or failed save reports storage and does not refresh the dashboard.
  reset();
  g_save_ok = false;
  CHECK(handle_command("RTL_ADSB_LOCATION 12.5 -45.5", true, hooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "save 125000000 -455000000");
  expect({"L:RTL_ADSB_LOCATION_ERROR storage"});

  // STOP: only when the ADS-B band is selected; otherwise the line is not handled here at all.
  reset();
  CHECK(handle_command("RTL_ADSB_STOP", false, hooks));
  CHECK(g_calls.size() == 2 && g_calls[0] == "band" && g_calls[1] == "stop");
  expect({"L:RTL_ADSB_STOPPING"});
  reset();
  g_band_adsb = false;
  CHECK(!handle_command("RTL_ADSB_STOP", true, hooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "band");
  expect({});

  // START needs no authentication and does not look at the band.
  reset();
  CHECK(handle_command("RTL_ADSB_START", false, hooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "start");
  expect({"L:RTL_ADSB_STARTING"});

  // Lines that are not this family's: exact match, and the prefixed routes need their trailing space.
  reset();
  for (const char* other : {"RTL_ADSB", "RTL_ADSB STATUS", "RTL_ADSB_RANGE_STATUS", "RTL_ADSB_RANGES", "RTL_ADSB_LOCATION",
                            "RTL_ADSB_STOP ", "RTL_ADSB_START now", "RTL_ADSB_STARTX", "rtl_adsb_start", ""}) {
    CHECK(!handle_command(other, true, hooks));
  }
  CHECK(g_out.empty());
  // Only the STOP prefix check could have called a hook, and only for an exact "RTL_ADSB_STOP".
  CHECK(g_calls.empty());

  if (failures == 0) std::printf("serial_adsb_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}

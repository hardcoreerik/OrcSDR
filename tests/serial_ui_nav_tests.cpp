// Host test for the on-screen navigation commands (serial_ui_nav.cpp): RTL_UI TAP, RTL_UI OPEN, RTL_RF24_PAGE. The
// expected replies are the strings the dispatcher in main.cpp used before the family moved. Every value here is made
// up; the names are the ones the original dispatch accepted.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_ui_nav_tests.cpp and apps/orcsdr-tab5/ui/serial_ui_nav.cpp
#include "serial_ui_nav.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_ui_nav;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write, "L:" a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;
bool g_dashboard_active = true;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.touch = [](int x, int y) { g_calls.push_back("touch " + std::to_string(x) + " " + std::to_string(y)); };
  hooks.open = [](Target t) { g_calls.push_back("open " + std::to_string(static_cast<int>(t))); };
  hooks.rf24_page_count = []() -> unsigned { return 5; };
  hooks.rf24_select_page = [](uint8_t page) -> bool {
    g_calls.push_back("page " + std::to_string(page));
    return g_dashboard_active;
  };
  return hooks;
}

bool run(const char* line, bool authenticated) {
  g_out.clear();
  g_calls.clear();
  return handle_command(line, authenticated, make_hooks());
}

void expect(const std::vector<std::string>& want) {
  if (g_out != want) {
    std::printf("FAIL: got %zu writes\n", g_out.size());
    for (const std::string& s : g_out) std::printf("  got  [%s]\n", s.c_str());
    for (const std::string& s : want) std::printf("  want [%s]\n", s.c_str());
    ++failures;
  }
}

void test_refusals() {
  const struct { const char* line; const char* reply; } cases[] = {
      {"RTL_UI TAP 10 20", "L:RTL_UI_TAP_ERROR auth_required"},
      {"RTL_UI TAP ", "L:RTL_UI_TAP_ERROR auth_required"},
      {"RTL_UI OPEN HOME", "L:RTL_UI_OPEN_ERROR auth_required"},
      {"RTL_UI OPEN nonsense", "L:RTL_UI_OPEN_ERROR auth_required"},
      {"RTL_RF24_PAGE 2", "L:RTL_RF24_PAGE_ERROR auth_required"},
      {"RTL_RF24_PAGE x", "L:RTL_RF24_PAGE_ERROR auth_required"},
  };
  for (const auto& c : cases) {
    CHECK(run(c.line, false));
    expect({c.reply});
    CHECK(g_calls.empty());
  }
}

void test_tap() {
  CHECK(run("RTL_UI TAP 640 360", true));
  expect({"W:RTL_UI_TAP_OK x=640 y=360\n"});
  CHECK(g_calls.size() == 1 && g_calls[0] == "touch 640 360");
  CHECK(run("RTL_UI TAP 0 0", true));
  expect({"W:RTL_UI_TAP_OK x=0 y=0\n"});
  CHECK(run("RTL_UI TAP 1279 719", true));
  expect({"W:RTL_UI_TAP_OK x=1279 y=719\n"});
  CHECK(g_calls.size() == 1 && g_calls[0] == "touch 1279 719");
  const char* const bad[] = {"RTL_UI TAP 1280 10", "RTL_UI TAP 10 720", "RTL_UI TAP -1 10", "RTL_UI TAP 10 -1",
                             "RTL_UI TAP 10", "RTL_UI TAP a b", "RTL_UI TAP ", "RTL_UI TAP  "};
  for (const char* line : bad) {
    CHECK(run(line, true));
    expect({"L:RTL_UI_TAP_INVALID use RTL_UI TAP <x 0-1279> <y 0-719>"});
    CHECK(g_calls.empty());
  }
}

void test_open() {
  // The same names, in the same order, as the original dispatch; the enum value is that order.
  const char* const names[] = {"HOME", "CLOCK", "FM", "AM", "SHORTWAVE", "AIRBAND", "CB", "P25", "ADSB", "LORA",
                               "RF_LAB", "WIFI_ANALYSIS", "FT8", "SETTINGS", "WEATHER", "POCSAG", "ACARS",
                               "DEAD_AIR", "AIS", "VISUALIZER", "MARINE", "SATELLITE", "UTILITIES"};
  int index = 0;
  for (const char* name : names) {
    const std::string line = std::string("RTL_UI OPEN ") + name;
    CHECK(run(line.c_str(), true));
    expect({std::string("W:RTL_UI_OPEN_OK target=") + name + "\n"});
    CHECK(g_calls.size() == 1 && g_calls[0] == "open " + std::to_string(index));
    ++index;
  }
  CHECK(index == 23);
  // Open is called before the OK reply, and the target is echoed as typed.
  const char* const bad[] = {"RTL_UI OPEN ", "RTL_UI OPEN home", "RTL_UI OPEN HOME ", "RTL_UI OPEN  HOME",
                             "RTL_UI OPEN SCOPE", "RTL_UI OPEN FM2", "RTL_UI OPEN F"};
  for (const char* line : bad) {
    CHECK(run(line, true));
    expect({"L:RTL_UI_OPEN_INVALID use HOME|FM|AM|SHORTWAVE|P25|ADSB|LORA|RF_LAB|WIFI_ANALYSIS|FT8|SETTINGS|WEATHER|CB|"
            "POCSAG|AIRBAND|ACARS|DEAD_AIR|AIS|VISUALIZER|MARINE|SATELLITE|UTILITIES"});
    CHECK(g_calls.empty());
  }
}

void test_rf24_page() {
  CHECK(run("RTL_RF24_PAGE 0", true));
  expect({"W:RTL_RF24_PAGE_OK page=0\n"});
  CHECK(g_calls.size() == 1 && g_calls[0] == "page 0");
  CHECK(run("RTL_RF24_PAGE 4", true));
  expect({"W:RTL_RF24_PAGE_OK page=4\n"});
  const char* const bad[] = {"RTL_RF24_PAGE 5", "RTL_RF24_PAGE x", "RTL_RF24_PAGE ", "RTL_RF24_PAGE 99"};
  for (const char* line : bad) {
    CHECK(run(line, true));
    expect({"L:RTL_RF24_PAGE_INVALID use 0-4"});
    CHECK(g_calls.empty());
  }
  g_dashboard_active = false;
  CHECK(run("RTL_RF24_PAGE 3", true));
  expect({"L:RTL_RF24_PAGE_ERROR dashboard_inactive"});
  CHECK(g_calls.size() == 1 && g_calls[0] == "page 3");
  g_dashboard_active = true;
}

void test_not_ours() {
  const char* const lines[] = {"", "RTL_UI TAP", "RTL_UI OPEN", "RTL_RF24_PAGE", "RTL_UI STATUS", "RTL_UI ACTION x",
                               "RTL_UI TAPX 1 2", "rtl_ui tap 1 2", "UI_TAP 1 2", "RTL_RF24 PAGE 1",
                               "RTL_RF24_PAGES 1", "RTL_UI  OPEN HOME"};
  for (const bool authenticated : {false, true}) {
    for (const char* line : lines) {
      CHECK(!run(line, authenticated));
      CHECK(g_out.empty());
      CHECK(g_calls.empty());
    }
  }
}

}  // namespace

int main() {
  test_refusals();
  test_tap();
  test_open();
  test_rf24_page();
  test_not_ours();
  if (failures == 0) std::printf("serial_ui_nav_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}

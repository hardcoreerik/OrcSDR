// Host test for the RTL_TUNE / RTL_FREQ command family (serial_tune.cpp). The expected replies are the strings the
// dispatcher in main.cpp used before the family moved. The frequencies are made-up host values; the module has no
// frequency limit of its own, so the test also checks that nothing is refused on the module's account.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_tune_tests.cpp and apps/orcsdr-tab5/ui/serial_tune.cpp
#include "serial_tune.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_tune;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_out;
std::vector<std::string> g_calls;
TuneResult g_tune_result = TuneResult::ok;
bool g_retune_ok = true;
const char* g_ui_band = "FM";

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.freq_status = []() -> FreqStatus {
    g_calls.push_back("status");
    return {g_ui_band, 98700000u, "WFM"};
  };
  hooks.hot_retune = [](uint32_t hz) -> bool {
    g_calls.push_back("retune:" + std::to_string(hz));
    g_ui_band = "AM";   // the band name is read after the retune
    return g_retune_ok;
  };
  hooks.ui_band_name = []() -> const char* {
    g_calls.push_back("band");
    return g_ui_band;
  };
  hooks.tune = [](const char* name, uint32_t hz, const char** label) -> TuneResult {
    g_calls.push_back(std::string("tune:") + name + ":" + std::to_string(hz));
    if (g_tune_result == TuneResult::ok) *label = "CB";
    return g_tune_result;
  };
  return hooks;
}

void reset() {
  g_out.clear();
  g_calls.clear();
  g_tune_result = TuneResult::ok;
  g_retune_ok = true;
  g_ui_band = "FM";
}

bool run(const char* line, bool authenticated) {
  const Hooks hooks = make_hooks();
  return handle_command(line, authenticated, hooks);
}

void test_tune() {
  reset();
  CHECK(run("RTL_TUNE CB 27185000", true));
  CHECK(g_calls.size() == 1 && g_calls[0] == "tune:CB:27185000");
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_TUNE_OK band=CB frequency_hz=27185000\n");

  reset();
  CHECK(run("RTL_TUNE FM", true));
  CHECK(g_calls.empty());
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_TUNE_INVALID usage: RTL_TUNE <BAND> <HZ>");
  reset();
  CHECK(run("RTL_TUNE FM abc", true));
  CHECK(g_calls.empty() && g_out[0] == "L:RTL_TUNE_INVALID usage: RTL_TUNE <BAND> <HZ>");
  reset();
  CHECK(run("RTL_TUNE ", true));
  CHECK(g_calls.empty() && g_out[0] == "L:RTL_TUNE_INVALID usage: RTL_TUNE <BAND> <HZ>");

  reset();
  g_tune_result = TuneResult::unknown_band;
  CHECK(run("RTL_TUNE XYZ 100", true));
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_TUNE_INVALID unknown band (FM|AM|WX|CB|LORA|BROWSE|ADSB|P25)");

  reset();
  g_tune_result = TuneResult::not_ready;
  CHECK(run("RTL_TUNE FM 98700000", true));
  CHECK(g_out.size() == 1 && g_out[0] == "L:RTL_TUNE_UNAVAILABLE device not ready");

  reset();
  g_tune_result = TuneResult::not_queued;   // refused listen: handled, no reply
  CHECK(run("RTL_TUNE FM 98700000", true));
  CHECK(g_calls.size() == 1 && g_out.empty());

  // The band name is cut to 15 characters by the scan; the rest ("PQRS") is not a number, so it is a usage error.
  reset();
  CHECK(run("RTL_TUNE ABCDEFGHIJKLMNOPQRS 5", true));
  CHECK(g_calls.empty() && g_out.size() == 1 && g_out[0] == "L:RTL_TUNE_INVALID usage: RTL_TUNE <BAND> <HZ>");
  reset();
  CHECK(run("RTL_TUNE ABCDEFGHIJKLMNO 5", true));
  CHECK(g_calls.size() == 1 && g_calls[0] == "tune:ABCDEFGHIJKLMNO:5");

  // The module does not cap the number: a value above 32 bits is passed on as the low 32 bits.
  reset();
  CHECK(run("RTL_TUNE AM 1620000", true));
  CHECK(g_calls[0] == "tune:AM:1620000" && g_out[0] == "W:RTL_TUNE_OK band=CB frequency_hz=1620000\n");

  // Not authenticated: not ours, nothing called.
  reset();
  CHECK(!run("RTL_TUNE CB 27185000", false));
  CHECK(g_calls.empty() && g_out.empty());
  // Exactness.
  reset();
  CHECK(!run("RTL_TUNE", true));
  CHECK(!run("RTL_TUNEX CB 1", true));
  CHECK(!run("rtl_tune CB 1", true));
  CHECK(g_calls.empty() && g_out.empty());
}

void test_freq_status() {
  reset();
  CHECK(run("RTL_FREQ", false));   // the status line needs no authentication
  CHECK(g_calls.size() == 1 && g_calls[0] == "status");
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_FREQ_STATUS band=FM frequency_hz=98700000 mode=WFM\n");
  reset();
  CHECK(run("RTL_FREQ", true));
  CHECK(g_out.size() == 1);
}

void test_freq_set() {
  reset();
  CHECK(run("RTL_FREQ 162550000", true));
  CHECK(g_calls.size() == 2 && g_calls[0] == "retune:162550000" && g_calls[1] == "band");
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_FREQ_OK band=AM frequency_hz=162550000\n");

  reset();
  g_retune_ok = false;
  CHECK(run("RTL_FREQ 162550000", true));
  CHECK(g_calls.size() == 2 && g_calls[0] == "retune:162550000" && g_calls[1] == "band");
  CHECK(g_out.size() == 1 && g_out[0] == "W:RTL_FREQ_REJECTED band=AM frequency_hz=162550000\n");

  reset();
  CHECK(run("RTL_FREQ 0", true));
  CHECK(g_calls.empty() && g_out.size() == 1 && g_out[0] == "L:RTL_FREQ_INVALID usage: RTL_FREQ <HZ>");
  reset();
  CHECK(run("RTL_FREQ abc", true));
  CHECK(g_calls.empty() && g_out[0] == "L:RTL_FREQ_INVALID usage: RTL_FREQ <HZ>");
  reset();
  CHECK(run("RTL_FREQ ", true));
  CHECK(g_calls.empty() && g_out[0] == "L:RTL_FREQ_INVALID usage: RTL_FREQ <HZ>");

  // strtoul keeps its own behaviour: leading text after the number is ignored.
  reset();
  CHECK(run("RTL_FREQ 7000000 extra", true));
  CHECK(g_calls[0] == "retune:7000000");

  // Not authenticated: not ours, nothing called.
  reset();
  CHECK(!run("RTL_FREQ 162550000", false));
  CHECK(g_calls.empty() && g_out.empty());
  CHECK(!run("RTL_FREQUENCY", true));
  CHECK(!run("RTL_FREQX 5", true));
  CHECK(!run("RTL_FREQ_STATUS", true));
  CHECK(g_calls.empty() && g_out.empty());
  CHECK(!run("", true));
  CHECK(!run("RTL_SIGNAL", true));
}

}  // namespace

int main() {
  test_tune();
  test_freq_status();
  test_freq_set();
  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("serial_tune tests passed\n");
  return 0;
}

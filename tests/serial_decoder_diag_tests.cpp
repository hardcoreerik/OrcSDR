// Host test for the decoder diagnostic command family (serial_decoder_diag.cpp): RTL_ACARS STATUS / SELFTEST /
// ARCHIVETEST, RTL_AIS STATUS / BENCH / TEST ON / TEST OFF, RTL_DROPLOG / RTL_DROPLOG CLEAR. The expected replies are
// the format strings the dispatcher in main.cpp used before the family moved, filled in with made-up values.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_decoder_diag_tests.cpp and apps/orcsdr-tab5/ui/serial_decoder_diag.cpp
#include "serial_decoder_diag.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_decoder_diag;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write, "L:" is a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;
bool g_alloc_fails = false;
bool g_alloc_live = false;
size_t g_alloc_capacity = 0;
char g_ais_buffer[1400];
bool g_test_replay = false;
uint32_t g_drop_count = 3;
AcarsSelfTest g_selftest;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.acars_describe = [](char* out, size_t capacity) {
    g_calls.push_back("acars_describe");
    std::snprintf(out, capacity, "ACARS app line");
  };
  hooks.acars_format_status = [](char* out, size_t capacity) {
    g_calls.push_back("acars_status");
    std::snprintf(out, capacity, "ACARS receiver line");
  };
  hooks.acars_selftest = []() -> AcarsSelfTest {
    g_calls.push_back("selftest");
    return g_selftest;
  };
  hooks.acars_archive_test = [](char* out, size_t capacity) {
    g_calls.push_back("archivetest");
    std::snprintf(out, capacity, "RTL_ACARS_ARCHIVETEST result=PASS cap=%u", static_cast<unsigned>(capacity));
  };
  hooks.ais_line_alloc = [](size_t capacity) -> char* {
    g_calls.push_back("ais_alloc");
    g_alloc_capacity = capacity;
    if (g_alloc_fails) return nullptr;
    g_alloc_live = true;
    return g_ais_buffer;
  };
  hooks.ais_line_free = [](char* line) {
    g_calls.push_back("ais_free");
    CHECK(line == g_ais_buffer);
    g_alloc_live = false;
  };
  hooks.ais_describe = [](char* out, size_t capacity) {
    g_calls.push_back("ais_describe");
    std::snprintf(out, capacity, "AIS app line");
  };
  hooks.ais_format_status = [](char* out, size_t capacity) {
    g_calls.push_back("ais_status");
    std::snprintf(out, capacity, "AIS radio line");
  };
  hooks.ais_dsp_stack_hwm = []() -> unsigned { return 2748; };
  hooks.ais_bench = [](char* out, size_t capacity) {
    g_calls.push_back("ais_bench");
    std::snprintf(out, capacity, "RTL_AIS_BENCH cap=%u", static_cast<unsigned>(capacity));
  };
  hooks.ais_set_test_replay = [](bool on) {
    g_calls.push_back(on ? "replay_on" : "replay_off");
    g_test_replay = on;
  };
  hooks.droplog_count = []() -> uint32_t { return g_drop_count; };
  hooks.droplog_capacity = 4;
  hooks.droplog_now_ms = []() -> uint32_t { return 90500; };
  hooks.droplog_stream_started_ms = []() -> uint32_t { return 12000; };
  hooks.droplog_entry_ms = [](uint32_t index) -> uint32_t { return 20000 + index * 1500; };
  hooks.droplog_entry_filled = [](uint32_t index) -> uint32_t { return 7 + index; };
  hooks.droplog_clear = []() {
    g_calls.push_back("droplog_clear");
    g_drop_count = 0;
  };
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

}  // namespace

int main() {
  const Hooks hooks = make_hooks();
  g_selftest = {true, 10, 10, 0, 0, 2400000, 1800000};

  // STATUS needs no authentication: two println lines.
  CHECK(handle_command("RTL_ACARS STATUS", false, hooks));
  expect({"L:ACARS app line", "L:ACARS receiver line"});
  CHECK(g_calls.size() == 2 && g_calls[0] == "acars_describe" && g_calls[1] == "acars_status");
  g_calls.clear();

  CHECK(handle_command("RTL_AIS STATUS", false, hooks));
  expect({"L:AIS app line", "L:AIS radio line", "W:RTL_AIS_TASK dsp_stack_hwm=2748\n"});
  CHECK(g_alloc_capacity == 1400 && !g_alloc_live);
  CHECK(g_calls.size() == 4 && g_calls[0] == "ais_alloc" && g_calls[3] == "ais_free");
  g_calls.clear();
  // No memory: one reply, nothing else called, nothing to free.
  g_alloc_fails = true;
  CHECK(handle_command("RTL_AIS STATUS", false, hooks));
  expect({"L:RTL_AIS_STATUS error=no_memory"});
  CHECK(g_calls.size() == 1 && g_calls[0] == "ais_alloc");
  g_calls.clear();
  g_alloc_fails = false;

  // DROPLOG: the header is one write, each entry its own write, then a bare newline written on its own.
  CHECK(handle_command("RTL_DROPLOG", false, hooks));
  expect({"W:RTL_DROPLOG now_ms=90500 stream_started_ms=12000 total_drops=3 first_3:", "W: 20000(f7)", "W: 21500(f8)",
          "W: 23000(f9)", "W:\n"});
  // More drops than the log holds: the count is the total, the list stops at the capacity.
  g_drop_count = 9;
  CHECK(handle_command("RTL_DROPLOG", true, hooks));
  expect({"W:RTL_DROPLOG now_ms=90500 stream_started_ms=12000 total_drops=9 first_4:", "W: 20000(f7)", "W: 21500(f8)",
          "W: 23000(f9)", "W: 24500(f10)", "W:\n"});
  g_drop_count = 0;
  CHECK(handle_command("RTL_DROPLOG", false, hooks));
  expect({"W:RTL_DROPLOG now_ms=90500 stream_started_ms=12000 total_drops=0 first_0:", "W:\n"});
  g_drop_count = 3;
  CHECK(handle_command("RTL_DROPLOG CLEAR", false, hooks));
  expect({"L:RTL_DROPLOG cleared"});
  CHECK(g_calls.size() == 1 && g_calls[0] == "droplog_clear" && g_drop_count == 0);
  g_calls.clear();

  // BENCH, TEST ON/OFF, SELFTEST and ARCHIVETEST refuse an unauthenticated caller before doing anything.
  CHECK(handle_command("RTL_AIS BENCH", false, hooks));
  expect({"L:RTL_AIS_BENCH error=auth_required"});
  CHECK(handle_command("RTL_AIS TEST ON", false, hooks));
  expect({"L:RTL_AIS_TEST_ERR auth_required"});
  CHECK(handle_command("RTL_AIS TEST OFF", false, hooks));
  expect({"L:RTL_AIS_TEST_ERR auth_required"});
  CHECK(handle_command("RTL_ACARS SELFTEST", false, hooks));
  expect({"L:RTL_ACARS_SELFTEST_ERROR auth_required"});
  CHECK(handle_command("RTL_ACARS ARCHIVETEST", false, hooks));
  expect({"L:RTL_ACARS_ARCHIVETEST_ERROR auth_required"});
  CHECK(g_calls.empty() && !g_test_replay);

  CHECK(handle_command("RTL_AIS BENCH", true, hooks));
  expect({"L:RTL_AIS_BENCH cap=400"});
  CHECK(handle_command("RTL_AIS TEST ON", true, hooks));
  expect({"W:RTL_AIS_TEST_OK test=1\n"});
  CHECK(g_test_replay);
  CHECK(handle_command("RTL_AIS TEST OFF", true, hooks));
  expect({"W:RTL_AIS_TEST_OK test=0\n"});
  CHECK(!g_test_replay);
  CHECK(g_calls.size() == 3 && g_calls[0] == "ais_bench" && g_calls[1] == "replay_on" && g_calls[2] == "replay_off");
  g_calls.clear();

  // SELFTEST: pass, then every way to fail, and the zero-samples case that prints a zero rate.
  CHECK(handle_command("RTL_ACARS SELFTEST", true, hooks));
  expect({"W:RTL_ACARS_SELFTEST allocated=1 cases=10 passed=10 missed=0 duplicates=0 samples=2400000 dsp_us=1800000 "
          "us_per_ksample=750 result=PASS\n"});
  g_selftest = {true, 10, 8, 1, 1, 2400000, 1800000};
  CHECK(handle_command("RTL_ACARS SELFTEST", true, hooks));
  expect({"W:RTL_ACARS_SELFTEST allocated=1 cases=10 passed=8 missed=1 duplicates=1 samples=2400000 dsp_us=1800000 "
          "us_per_ksample=750 result=FAIL\n"});
  g_selftest = {false, 0, 0, 0, 0, 0, 0};
  CHECK(handle_command("RTL_ACARS SELFTEST", true, hooks));
  expect({"W:RTL_ACARS_SELFTEST allocated=0 cases=0 passed=0 missed=0 duplicates=0 samples=0 dsp_us=0 "
          "us_per_ksample=0 result=FAIL\n"});
  g_calls.clear();

  CHECK(handle_command("RTL_ACARS ARCHIVETEST", true, hooks));
  expect({"L:RTL_ACARS_ARCHIVETEST result=PASS cap=200"});
  CHECK(g_calls.size() == 1 && g_calls[0] == "archivetest");
  g_calls.clear();

  // Anything else is left to the dispatcher: exact matches stay exact.
  for (const char* other : {"RTL_ACARS", "RTL_ACARS ", "RTL_ACARS STATUS ", "RTL_ACARS STATUSX", "rtl_acars status",
                            "RTL_ACARS SELFTEST 1", "RTL_ACARS ARCHIVETEST ", "RTL_AIS", "RTL_AIS STATUS ",
                            "RTL_AIS BENCH 1", "RTL_AIS TEST", "RTL_AIS TEST ON ", "RTL_AIS TEST ONE", "RTL_AIS TEST OFF ",
                            "RTL_DROPLOG ", "RTL_DROPLOG CLEARX", "RTL_DROPLOGX", "RTL_DROPLOG clear", ""}) {
    CHECK(!handle_command(other, true, hooks));
  }
  CHECK(g_out.empty() && g_calls.empty());

  if (failures == 0) std::printf("PASS: RTL_ACARS STATUS/SELFTEST/ARCHIVETEST, RTL_AIS STATUS/BENCH/TEST, RTL_DROPLOG replies, refusals and call order\n");
  return failures == 0 ? 0 : 1;
}

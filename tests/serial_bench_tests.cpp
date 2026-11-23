// Host test for the RTL_SD_SELF_CHECK, RTL_SD_BENCH and RTL_WIFI_C6_POWER OFF commands (serial_bench.cpp). The
// expected replies are the strings the dispatcher in main.cpp used before the family moved. Fake hooks only.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_bench_tests.cpp and apps/orcsdr-tab5/ui/serial_bench.cpp
#include "serial_bench.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_bench;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_log;   // every write and every device action, in order

Hooks make_hooks() {
  Hooks hooks;
  hooks.write_line = [](const char* text) { g_log.push_back(std::string("L:") + text); };
  hooks.sd_self_check = []() { g_log.push_back("selfcheck"); };
  hooks.sd_bench = [](unsigned mib) { g_log.push_back("bench " + std::to_string(mib)); };
  hooks.c6_power_off = []() { g_log.push_back("c6off"); };
  return hooks;
}

bool run(const char* line, bool authenticated) {
  g_log.clear();
  return handle_command(line, authenticated, make_hooks());
}

bool log_is(const std::vector<std::string>& expected) { return g_log == expected; }

void test_self_check() {
  CHECK(run("RTL_SD_SELF_CHECK", true));
  CHECK(log_is({"selfcheck"}));
  CHECK(run("RTL_SD_SELF_CHECK", false));
  CHECK(log_is({"L:RTL_SD_SELF_CHECK_ERROR auth_required"}));
  CHECK(!run("RTL_SD_SELF_CHECK ", true));
  CHECK(g_log.empty());
  CHECK(!run("RTL_SD_SELF_CHEC", true));
  CHECK(!run("rtl_sd_self_check", true));
  CHECK(g_log.empty());
}

void test_bench() {
  CHECK(run("RTL_SD_BENCH", true));
  CHECK(log_is({"bench 32"}));
  CHECK(run("RTL_SD_BENCH 4", true));
  CHECK(log_is({"bench 4"}));
  CHECK(run("RTL_SD_BENCH 64", true));
  CHECK(log_is({"bench 64"}));
  CHECK(run("RTL_SD_BENCH 16 extra", true));
  CHECK(log_is({"bench 16"}));
  CHECK(run("RTL_SD_BENCH 0x10", false) && log_is({"L:RTL_SD_BENCH_ERROR auth_required"}));
  CHECK(run("RTL_SD_BENCH", false) && log_is({"L:RTL_SD_BENCH_ERROR auth_required"}));
  const char* invalid[] = {"RTL_SD_BENCH 3", "RTL_SD_BENCH 65", "RTL_SD_BENCH 0", "RTL_SD_BENCH abc", "RTL_SD_BENCH ",
                           "RTL_SD_BENCH X", "RTL_SD_BENCH8", "RTL_SD_BENCH -8", "RTL_SD_BENCHMARK"};
  for (const char* line : invalid) {
    CHECK(run(line, true));
    CHECK(log_is({"L:RTL_SD_BENCH_INVALID use RTL_SD_BENCH [4..64 MiB]"}));
  }
  // Unauthenticated wins over a bad size, and nothing runs.
  CHECK(run("RTL_SD_BENCH 999", false) && log_is({"L:RTL_SD_BENCH_ERROR auth_required"}));
  // The prefix match is the original's: any non-space byte after the name is a bad size.
  CHECK(run("RTL_SD_BENCH\t", true));
  CHECK(log_is({"L:RTL_SD_BENCH_INVALID use RTL_SD_BENCH [4..64 MiB]"}));
}

void test_c6() {
  CHECK(run("RTL_WIFI_C6_POWER OFF", true));
  CHECK(log_is({"c6off", "L:RTL_WIFI_C6_POWER off"}));
  CHECK(run("RTL_WIFI_C6_POWER OFF", false));
  CHECK(log_is({"L:RTL_WIFI_C6_POWER_ERROR auth_required"}));
  CHECK(!run("RTL_WIFI_C6_POWER OFF ", true));
  CHECK(!run("RTL_WIFI_C6_POWER ON", true));
  CHECK(!run("RTL_WIFI_C6_POWER", true));
  CHECK(!run("RTL_WIFI_C6_POWER off", true));
  CHECK(g_log.empty());
}

void test_unrelated() {
  CHECK(!run("", true));
  CHECK(!run("RTL_SD", true));
  CHECK(!run("RTL_SPLASH_GATE OFF", true));
  CHECK(!run("RTL_WIFI_STATUS", true));
  CHECK(g_log.empty());
}

}  // namespace

int main() {
  test_self_check();
  test_bench();
  test_c6();
  test_unrelated();
  if (failures == 0) std::printf("serial_bench tests passed\n");
  return failures == 0 ? 0 : 1;
}

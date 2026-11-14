// Host tests for RTL_DISPLAY_BENCH parsing (apps/orcsdr-tab5/ui/serial_display_bench.cpp).
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_display_bench_tests.cpp apps/orcsdr-tab5/ui/serial_display_bench.cpp
#include "serial_display_bench.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_display_bench;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_lines;
std::vector<unsigned> g_runs;

const Hooks kHooks = [] {
  Hooks h;
  h.write_line = [](const char* t) { g_lines.emplace_back(t); };
  h.run = [](unsigned s) { g_runs.push_back(s); };
  return h;
}();

void reset() {
  g_lines.clear();
  g_runs.clear();
}

}  // namespace

int main() {
  reset();
  CHECK(handle_command("RTL_DISPLAY_BENCH", false, kHooks));
  CHECK(g_lines.size() == 1 && g_lines[0] == "RTL_DISPLAY_BENCH_ERROR auth_required" && g_runs.empty());
  reset();
  CHECK(handle_command("RTL_DISPLAY_BENCH 5", false, kHooks));
  CHECK(g_lines.size() == 1 && g_lines[0] == "RTL_DISPLAY_BENCH_ERROR auth_required" && g_runs.empty());

  reset();
  CHECK(handle_command("RTL_DISPLAY_BENCH", true, kHooks));
  CHECK(g_lines.empty() && g_runs.size() == 1 && g_runs[0] == 2);
  reset();
  CHECK(handle_command("RTL_DISPLAY_BENCH 1", true, kHooks));
  CHECK(handle_command("RTL_DISPLAY_BENCH 10", true, kHooks));
  CHECK(g_runs.size() == 2 && g_runs[0] == 1 && g_runs[1] == 10);

  for (const char* bad : {"RTL_DISPLAY_BENCH 0", "RTL_DISPLAY_BENCH 11", "RTL_DISPLAY_BENCH x", "RTL_DISPLAY_BENCH 3x",
                          "RTL_DISPLAY_BENCH ", "RTL_DISPLAY_BENCH -1"}) {
    reset();
    CHECK(handle_command(bad, true, kHooks));
    CHECK(g_runs.empty() && g_lines.size() == 1 && g_lines[0] == "RTL_DISPLAY_BENCH_ERROR usage=RTL_DISPLAY_BENCH [1-10]");
  }
  for (const char* other : {"RTL_DISPLAY_BENCHX", "RTL_DISPLAY", "rtl_display_bench", "RTL_DSP STATS"}) {
    reset();
    CHECK(!handle_command(other, true, kHooks));
    CHECK(g_runs.empty() && g_lines.empty());
  }
  if (failures) {
    std::printf("serial_display_bench_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("serial_display_bench_tests: ok\n");
  return 0;
}

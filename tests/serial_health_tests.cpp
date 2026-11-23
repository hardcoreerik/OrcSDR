// Host test for the RTL_HEALTH command (serial_health.cpp). The expected reply is the format string the dispatcher in
// main.cpp used before the command moved, filled in with made-up figures.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_health_tests.cpp and apps/orcsdr-tab5/ui/serial_health.cpp
#include "serial_health.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_health;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_out;
int g_snapshots = 0;
bool g_big = false;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.snapshot = []() -> Snapshot {
    ++g_snapshots;
    Snapshot s;
    if (g_big) {
      s.uptime_ms = 4294967295u;
      s.free_heap = 4294967294u;
      s.min_free_heap = 4294967293u;
      s.dma_free = 4294967292u;
      s.dma_min = 4294967291u;
      s.dma_largest = 4294967290u;
      s.tasks = 4294967289u;
      s.main_stack_hwm = 4294967288u;
      s.reset_reason = -7;
      s.internal_free = 4294967287u;
      s.internal_min = 4294967286u;
      s.internal_largest = 4294967285u;
      s.psram_free = 4294967284u;
      s.psram_largest = 4294967283u;
      return s;
    }
    s.uptime_ms = 123456;
    s.free_heap = 301000;
    s.min_free_heap = 250000;
    s.dma_free = 45000;
    s.dma_min = 21000;
    s.dma_largest = 30000;
    s.tasks = 37;
    s.main_stack_hwm = 4100;
    s.reset_reason = 4;
    s.internal_free = 280000;
    s.internal_min = 230000;
    s.internal_largest = 120000;
    s.psram_free = 20900000;
    s.psram_largest = 16000000;
    return s;
  };
  return hooks;
}

bool run(const char* command, bool authenticated = true) {
  g_out.clear();
  g_snapshots = 0;
  return handle_command(command, authenticated, make_hooks());
}

void test_reply() {
  g_big = false;
  CHECK(run("RTL_HEALTH"));
  CHECK(g_snapshots == 1);
  CHECK(g_out.size() == 1);
  CHECK(!g_out.empty() &&
        g_out[0] ==
            "W:RTL_HEALTH_STATUS uptime_ms=123456 free_heap=301000 min_free_heap=250000 "
            "dma_free=45000 dma_min=21000 dma_largest=30000 tasks=37 main_stack_hwm=4100 "
            "reset_reason=4 internal_free=280000 internal_min=230000 internal_largest=120000 "
            "psram_free=20900000 psram_largest=16000000\n");
  CHECK(run("RTL_HEALTH", false));   // read-only: no authentication needed
  CHECK(g_out.size() == 1 && g_out[0].rfind("W:RTL_HEALTH_STATUS uptime_ms=123456 ", 0) == 0);
}

void test_extremes() {
  g_big = true;
  CHECK(run("RTL_HEALTH"));
  CHECK(g_out.size() == 1);
  CHECK(!g_out.empty() &&
        g_out[0] ==
            "W:RTL_HEALTH_STATUS uptime_ms=4294967295 free_heap=4294967294 min_free_heap=4294967293 "
            "dma_free=4294967292 dma_min=4294967291 dma_largest=4294967290 tasks=4294967289 "
            "main_stack_hwm=4294967288 reset_reason=-7 internal_free=4294967287 internal_min=4294967286 "
            "internal_largest=4294967285 psram_free=4294967284 psram_largest=4294967283\n");
  g_big = false;
}

void test_no_match() {
  CHECK(!run("RTL_HEALTH "));
  CHECK(!run("RTL_HEALTH STATUS"));
  CHECK(!run("RTL_HEALT"));
  CHECK(!run("rtl_health"));
  CHECK(!run("RTL_HEALTHX"));
  CHECK(!run(""));
  CHECK(g_snapshots == 0 && g_out.empty());
}

}  // namespace

int main() {
  test_reply();
  test_extremes();
  test_no_match();
  if (failures == 0) std::printf("serial_health tests passed\n");
  return failures == 0 ? 0 : 1;
}

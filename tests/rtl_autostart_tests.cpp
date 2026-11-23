// Host test for rtl_autostart: the remote-boot start and its 30 s retry, with a fake clock and fake conditions.
#include "rtl_autostart.hpp"

#include <cstdio>

namespace {
namespace ra = orcsdr::rtl_autostart;
int g_failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); ++g_failures; } } while (0)

uint32_t g_now = 0;
bool g_failed = false, g_boot = true, g_paused = false, g_usb = false, g_remote = true, g_busy = false, g_requested = false;
int g_reads = 0;

ra::Hooks hooks() {
  ra::Hooks h;
  h.now_ms = []() { return g_now; };
  h.capture_failed = []() { ++g_reads; return g_failed; };
  h.boot_complete = []() { ++g_reads; return g_boot; };
  h.io_paused = []() { ++g_reads; return g_paused; };
  h.usb_device_ready = []() { ++g_reads; return g_usb; };
  h.remote_source_ready = []() { ++g_reads; return g_remote; };
  h.capture_running_or_queued = []() { ++g_reads; return g_busy; };
  h.capture_requested = []() { ++g_reads; return g_requested; };
  return h;
}
void reset() { g_now = 1000; g_failed = false; g_boot = true; g_paused = false; g_usb = false; g_remote = true; g_busy = false; g_requested = false; g_reads = 0; }
}  // namespace

int main() {
  const auto h = hooks();
  {  // first attempt, once
    reset(); ra::State s;
    CHECK(ra::decide(s, h) == ra::Start::first);
    CHECK(s.started && s.retry_ms == 1000);
    CHECK(ra::decide(s, h) == ra::Start::none);   // not failed: no retry, no first
  }
  {  // nothing before boot completes, with USB, with the radio paused, without a usable remote, busy, or already requested
    for (int what = 0; what < 6; ++what) {
      reset(); ra::State s;
      if (what == 0) g_boot = false;
      if (what == 1) g_usb = true;
      if (what == 2) g_paused = true;
      if (what == 3) g_remote = false;
      if (what == 4) g_busy = true;
      if (what == 5) g_requested = true;
      CHECK(ra::decide(s, h) == ra::Start::none);
      CHECK(!s.started);
    }
  }
  {  // a failed first attempt is retried after exactly 30 s, not before, and again 30 s after that
    reset(); ra::State s;
    CHECK(ra::decide(s, h) == ra::Start::first);
    g_failed = true;
    g_now = 1000 + 29999;
    CHECK(ra::decide(s, h) == ra::Start::none);
    g_now = 1000 + 30000;
    CHECK(ra::decide(s, h) == ra::Start::retry);
    CHECK(s.retry_ms == 31000);
    g_now = 31000 + 29999;
    CHECK(ra::decide(s, h) == ra::Start::none);
    g_now = 61000;
    CHECK(ra::decide(s, h) == ra::Start::retry);
  }
  {  // a retry still honours the other conditions
    reset(); ra::State s; ra::decide(s, h);
    g_failed = true; g_now += 40000; g_usb = true;
    CHECK(ra::decide(s, h) == ra::Start::none);
    g_usb = false; g_requested = true;
    CHECK(ra::decide(s, h) == ra::Start::none);
    g_requested = false;
    CHECK(ra::decide(s, h) == ra::Start::retry);
  }
  {  // the clock wrapping round does not break the 30 s test
    reset(); ra::State s; g_now = 0xFFFFFF00u;
    CHECK(ra::decide(s, h) == ra::Start::first);
    g_failed = true; g_now = 0xFFFFFF00u + 30000u;
    CHECK(ra::decide(s, h) == ra::Start::retry);
  }
  {  // the cheap path: once started and not failed, only the failed flag is read
    reset(); ra::State s; ra::decide(s, h); g_reads = 0;
    ra::decide(s, h);
    CHECK(g_reads == 1);
  }
  if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; }
  std::puts("PASS rtl_autostart");
  return 0;
}

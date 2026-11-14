// Host tests for the capture buffer release rules (apps/orcsdr-tab5/ui/buffer_claim.hpp).
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/buffer_claim_tests.cpp
#include "buffer_claim.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace orcsdr::buffer_claim;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

int g_waits = 0;
Claim* g_blocked = nullptr;
void wait_then_unblock() {
  ++g_waits;
  if (g_waits == 3 && g_blocked) g_blocked->give();   // stands in for the other task finishing its release
}

void test_releasable() {
  CaptureState s;
  CHECK(!releasable(s));                       // nothing allocated: nothing to give back
  s.allocated = true;
  CHECK(releasable(s));                        // allocated, never used
  s.active = true;
  CHECK(!releasable(s));                       // a capture is running
  s.active = false;
  s.writing = true;
  CHECK(!releasable(s));                       // the writer has not left the buffer yet
  s.writing = false;
  s.pending = true;
  CHECK(!releasable(s));                       // an export is queued
  s.pending = false;
  s.samples = 1;
  CHECK(!releasable(s));                       // one unsaved sample is enough to keep it
  s.saved = true;
  CHECK(releasable(s));                        // saved: safe to drop
  s.samples = 3840000;
  s.saved = false;
  CHECK(!releasable(s));                       // a full capture waiting for an SD card is kept
  s.saved = true;
  s.active = true;
  CHECK(!releasable(s));                       // a new capture over an old saved one
  s.active = false;
  s.allocated = false;
  CHECK(!releasable(s));
}

void test_claim() {
  Claim claim;
  CHECK(!claim.held());
  CHECK(claim.try_take());
  CHECK(claim.held());
  CHECK(!claim.try_take());                    // a release that finds it busy must give up
  CHECK(claim.held());                         // and must not have disturbed the owner
  claim.give();
  CHECK(!claim.held());
  {
    CHECK(claim.try_take());
    Hold hold(claim);
    CHECK(!claim.try_take());
  }
  CHECK(!claim.held());                        // Hold gave it back on every exit path
  CHECK(claim.try_take());
  g_blocked = &claim;
  g_waits = 0;
  claim.take(wait_then_unblock);               // waits for the other owner, then owns it
  CHECK(g_waits == 3 && claim.held());
  claim.give();
  claim.take(nullptr);                         // free: no wait callback needed
  CHECK(claim.held());
  claim.give();
}

// A model of the firmware's three parties: a writer that follows the DSP handshake (raise the flag, look at `active`
// again, write, lower the flag), an owner that starts and stops captures, and a reclaimer on another thread. The buffer
// is poisoned (pointer nulled, memory freed) on release, so a write after release is caught by the address sanitizer.
void test_release_never_races_a_writer() {
  constexpr size_t kSamples = 64;
  Claim claim;
  std::atomic<short*> buffer{nullptr};
  std::atomic<bool> active{false};
  std::atomic<bool> writing{false};
  std::atomic<size_t> written{0};
  std::atomic<bool> saved{false};
  std::atomic<bool> done{false};
  std::atomic<unsigned> releases{0};
  std::atomic<unsigned> blocks{0};

  std::thread writer([&] {
    while (!done.load()) {
      if (!active.load()) continue;
      writing.store(true);
      if (active.load()) {
        short* p = buffer.load();
        size_t w = written.load();
        for (int i = 0; i < 8 && w < kSamples; ++i) p[w++] = static_cast<short>(i);
        written.store(w);
        if (w >= kSamples) active.store(false);
        blocks.fetch_add(1);
      }
      writing.store(false);
    }
  });
  std::thread reclaimer([&] {
    while (!done.load()) {
      if (!claim.try_take()) continue;
      Hold hold(claim);
      CaptureState s;
      s.allocated = buffer.load() != nullptr;
      s.active = active.load();
      s.writing = writing.load();
      s.samples = written.load();
      s.saved = saved.load();
      if (!releasable(s)) continue;
      short* p = buffer.exchange(nullptr);
      written.store(0);
      std::free(p);
      releases.fetch_add(1);
    }
  });
  for (int round = 0; round < 4000; ++round) {
    {
      claim.take([] { std::this_thread::yield(); });
      Hold hold(claim);
      if (buffer.load() == nullptr) buffer.store(static_cast<short*>(std::malloc(kSamples * sizeof(short))));
      written.store(0);
      saved.store(false);
      active.store(true);
    }
    std::this_thread::yield();
    {
      claim.take([] { std::this_thread::yield(); });
      Hold hold(claim);
      active.store(false);
      while (writing.load()) std::this_thread::yield();
      saved.store(true);                       // "exported": from here the reclaimer may take it
    }
  }
  done.store(true);
  writer.join();
  reclaimer.join();
  std::free(buffer.load());
  std::printf("model: %u writer blocks, %u releases\n", blocks.load(), releases.load());
  CHECK(releases.load() > 0);
}

}  // namespace

int main() {
  test_releasable();
  test_claim();
  test_release_never_races_a_writer();
  if (failures) {
    std::printf("buffer_claim_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("buffer_claim_tests: ok\n");
  return 0;
}

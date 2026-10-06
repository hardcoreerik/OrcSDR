#include "../src/control/link_policy.hpp"
#include <cassert>
#include <iostream>
#include <set>

using namespace orc::secure;

static bool acked = false;
static bool evidence() { return acked; }

static void planner_visits_every_channel_and_prefers_remembered() {
  ChannelPlanner none;
  std::set<uint8_t> seen;
  uint32_t dwell = 0;
  for (int i = 0; i < 11; ++i) { seen.insert(none.next(dwell)); assert(dwell == ChannelPlanner::kSweepDwellMs); }
  assert(seen.size() == 11 && *seen.begin() == 1 && *seen.rbegin() == 11);

  ChannelPlanner planner;
  planner.preferred(11);
  seen.clear();
  int preferred_visits = 0;
  for (int i = 0; i < 40; ++i) {
    const uint8_t channel = planner.next(dwell);
    seen.insert(channel);
    if (channel == 11) { ++preferred_visits; assert(dwell == ChannelPlanner::kPreferredDwellMs); }
    else assert(dwell == ChannelPlanner::kSweepDwellMs);
  }
  assert(seen.size() == 11);                 // a wrong remembered channel never hides the others
  assert(preferred_visits == 20);            // every second dwell
  ChannelPlanner invalid;
  invalid.preferred(0); invalid.preferred(12);
  for (int i = 0; i < 11; ++i) { invalid.next(dwell); assert(dwell == ChannelPlanner::kSweepDwellMs); }
}

static void lock_needs_acknowledged_traffic() {
  LockJudge judge;
  acked = false;
  assert(!judge.locked(false, 0, evidence));
  assert(judge.locked(true, 1000, evidence));      // fresh lock gets a grace period
  assert(judge.locked(true, 3000, evidence));
  assert(!judge.locked(true, 3600, evidence));     // adjacent-channel lock: never acknowledged
  acked = true;
  assert(judge.locked(true, 3700, evidence));      // the right channel keeps its lock
  acked = false;
  assert(!judge.locked(false, 4000, evidence));    // released lock
  assert(judge.locked(true, 4100, evidence));      // a new lock restarts the grace period
  LockJudge fixed_channel;
  assert(fixed_channel.locked(true, 0, nullptr));  // the tablet has no evidence source
  assert(fixed_channel.locked(true, 100000, nullptr));
  assert(!fixed_channel.locked(false, 100001, nullptr));
}

static void retry_is_bounded_and_respects_user_intent() {
  RetryScheduler retry;
  uint32_t now = 1000;
  assert(!retry.step(false, true, now));            // no intent: never retries
  retry.intent(true);
  assert(!retry.step(false, false, now));           // not timed out
  assert(!retry.step(false, true, now));            // arms the first delay (3 s)
  assert(!retry.step(false, true, now + 2999));
  assert(retry.step(false, true, now + 3000));
  assert(retry.retries() == 1);
  assert(!retry.step(false, false, now + 3001));    // attempt in progress
  const uint32_t expected[] = {5000, 10000, 20000, 30000, 30000};
  now += 3001;
  for (uint8_t i = 0; i < 5; ++i) {
    assert(!retry.step(false, true, now));
    assert(!retry.step(false, true, now + expected[i] - 1));
    assert(retry.step(false, true, now + expected[i]));
    now += expected[i] + 1;
    assert(!retry.step(false, false, now));
  }
  assert(retry.retries() == RetryScheduler::kMaxRetries);
  assert(!retry.step(false, true, now));            // exhausted: stays failed for the user
  assert(!retry.step(false, true, now + 100000));

  retry.intent(true);                               // an explicit Connect starts afresh
  assert(!retry.step(false, true, now));
  assert(retry.step(false, true, now + 3000));
  assert(!retry.step(true, false, now + 3001));     // connected clears the count
  assert(retry.retries() == 0);

  retry.intent(true);
  assert(!retry.step(false, true, now + 10000));
  retry.intent(false);                              // Disconnect / Forget / pairing
  assert(!retry.step(false, true, now + 10000 + 3000));
  assert(!retry.step(false, true, now + 100000));

  RetryScheduler wrapped;                           // millis() wrap-around
  wrapped.intent(true);
  const uint32_t near_wrap = 0xfffffff0u;
  assert(!wrapped.step(false, true, near_wrap));
  assert(!wrapped.step(false, true, near_wrap + 2999));
  assert(wrapped.step(false, true, near_wrap + 3000));
}

int main() {
  planner_visits_every_channel_and_prefers_remembered();
  lock_needs_acknowledged_traffic();
  retry_is_bounded_and_respects_user_intent();
  std::cout << "PASS: channel planner, evidence-based lock and bounded retry policy\n";
}

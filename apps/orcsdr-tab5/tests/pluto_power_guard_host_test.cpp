#include "pluto_power_guard.hpp"

#include <cstdio>

using namespace orcsdr::pluto_power_guard;

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

int main() {
  // A full battery, discharging or charging, does nothing.
  {
    State s;
    CHECK(!update(s, 100, 0));
    CHECK(!update(s, 100, 1));
    CHECK(s.level == Level::ok);
  }
  // One low reading is never enough: not for "low", not for "critical", even at 0%.
  {
    State s;
    CHECK(!update(s, 5, 0));
    CHECK(s.level == Level::ok);
    CHECK(!update(s, 0, 0) == false);  // second reading: low (2 in a row)
    CHECK(s.level == Level::low);      // but critical needs 3
  }
  // A single dip is forgotten: a good reading resets the count.
  {
    State s;
    CHECK(!update(s, 8, 0));
    CHECK(!update(s, 60, 0));  // a normal reading in between
    CHECK(!update(s, 8, 0));   // count restarts at 1
    CHECK(s.level == Level::ok);
    CHECK(!update(s, 60, 1));
    CHECK(!update(s, 8, 0));
    CHECK(s.level == Level::ok);
  }
  // Sustained: two readings at or below 20% is "low", three at or below 10% is "critical".
  {
    State s;
    CHECK(!update(s, 19, 0));
    CHECK(update(s, 18, 0));
    CHECK(s.level == Level::low);
    CHECK(!update(s, 12, 0));  // still low
    CHECK(!update(s, 9, 0));   // critical run is 1
    CHECK(!update(s, 8, 0));   // 2
    CHECK(update(s, 7, 0));    // 3: critical
    CHECK(s.level == Level::critical);
    CHECK(!update(s, 5, 0));   // stays, no repeat event
  }
  // Clearing: charging clears at any level; otherwise the level must be back at 25% (hysteresis), not just above the trip level.
  {
    State s;
    update(s, 9, 0); update(s, 9, 0); update(s, 9, 0);
    CHECK(s.level == Level::critical);
    CHECK(!update(s, 14, 0));
    CHECK(!update(s, 24, 0));
    CHECK(s.level != Level::ok);
    CHECK(update(s, 25, 0));
    CHECK(s.level == Level::ok);
  }
  {
    State s;
    update(s, 9, 0); update(s, 9, 0); update(s, 9, 0);
    CHECK(update(s, 9, 1));  // plugged in
    CHECK(s.level == Level::ok);
    CHECK(!update(s, 9, 1)); // charging: stays ok
    CHECK(!update(s, 9, 0)); // unplugged while low: counts start again
    CHECK(s.level == Level::ok);
  }
  // Unknown readings change nothing and do not count, in either direction.
  {
    State s;
    CHECK(!update(s, -1, 0));
    CHECK(!update(s, 5, -1));
    CHECK(!update(s, -1, -1));
    update(s, 9, 0); update(s, 9, 0);
    CHECK(s.level == Level::low);
    CHECK(!update(s, -1, 1));   // an unknown reading does not clear it
    CHECK(!update(s, 90, -1));
    CHECK(s.level == Level::low);
    update(s, 8, 0);            // the third real reading in a row ...
    update(s, 8, 0);            // (unknown ones did not break the run) ... counts toward critical
    CHECK(s.level == Level::critical);
  }
  // The counters cannot overflow on a long discharge.
  {
    State s;
    for (int i = 0; i < 1000; ++i) update(s, 3, 0);
    CHECK(s.level == Level::critical);
    CHECK(update(s, 3, 1));
    CHECK(s.level == Level::ok);
  }

  if (g_failures == 0) std::printf("pluto_power_guard_host_test: all passed\n");
  return g_failures == 0 ? 0 : 1;
}

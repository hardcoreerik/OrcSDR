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
  // A full battery, discharging or charging, never does anything.
  {
    State s;
    CHECK(update(s, 100, 0) == Event::none);
    CHECK(update(s, 100, 1) == Event::none);
    CHECK(!s.tripped);
  }
  // Discharging down the curve: one warning, then a trip, then nothing more while it stays low.
  {
    State s;
    CHECK(update(s, 30, 0) == Event::none);
    CHECK(update(s, 20, 0) == Event::warn);
    CHECK(update(s, 19, 0) == Event::none);  // warned once only
    CHECK(update(s, 11, 0) == Event::none);
    CHECK(update(s, 10, 0) == Event::trip);
    CHECK(s.tripped);
    CHECK(update(s, 9, 0) == Event::none);
    CHECK(update(s, 3, 0) == Event::none);
    CHECK(s.tripped);
  }
  // Charging clears a trip at any level; a level at or above the recover threshold clears it too (hysteresis between 10 and 25).
  {
    State s;
    CHECK(update(s, 8, 0) == Event::trip);
    CHECK(update(s, 12, 0) == Event::none);  // rose a little, still discharging: stays tripped
    CHECK(update(s, 24, 0) == Event::none);
    CHECK(s.tripped);
    CHECK(update(s, 25, 0) == Event::recover);
    CHECK(!s.tripped);
  }
  {
    State s;
    CHECK(update(s, 5, 0) == Event::trip);
    CHECK(update(s, 5, 1) == Event::recover);  // plugged in
    CHECK(!s.tripped);
    CHECK(update(s, 5, 1) == Event::none);     // charging: no trip
    CHECK(update(s, 5, 0) == Event::trip);     // unplugged again while low
  }
  // Unknown readings never trip, and never clear a trip.
  {
    State s;
    CHECK(update(s, -1, 0) == Event::none);
    CHECK(update(s, 5, -1) == Event::none);
    CHECK(update(s, -1, -1) == Event::none);
    CHECK(!s.tripped);
    CHECK(update(s, 5, 0) == Event::trip);
    CHECK(update(s, -1, 1) == Event::none);
    CHECK(update(s, 90, -1) == Event::none);
    CHECK(s.tripped);
  }
  // The warning re-arms after the battery has been well above the warning level, or charging.
  {
    State s;
    CHECK(update(s, 18, 0) == Event::warn);
    CHECK(update(s, 18, 0) == Event::none);
    CHECK(update(s, 50, 1) == Event::none);
    CHECK(update(s, 18, 0) == Event::warn);
  }
  // Edge values.
  {
    State s;
    CHECK(update(s, 0, 0) == Event::trip);
    State t;
    CHECK(update(t, 21, 0) == Event::none);
    CHECK(update(t, 10, 0) == Event::trip);  // straight to a trip without a warning is fine
  }

  if (g_failures == 0) std::printf("pluto_power_guard_host_test: all passed\n");
  return g_failures == 0 ? 0 : 1;
}

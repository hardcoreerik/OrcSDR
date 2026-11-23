// Session facts: counts by kind, extremes of the measured level, measured time that excludes gaps, the longest quiet
// stretch (including the one still running), and the distinct channels visited.
#include <cassert>
#include <cstdio>

#include "dead_air_stats.hpp"

using namespace orcsdr::dead_air;

namespace {
Event event(Kind kind, uint32_t ms) {
  Event e{};
  e.kind = kind;
  e.monotonic_ms = ms;
  return e;
}
}  // namespace

int main() {
  SessionStats stats;
  stats.reset(10000);
  SessionSummary s = stats.summary(10000, 0);
  assert(s.elapsed_s == 0 && !s.have_level && s.events_rf == 0 && s.channels == 0 && s.longest_quiet_s == 0);

  // Levels: extremes of valid samples only; time between consecutive valid samples is measured time, a gap is not.
  for (uint32_t t = 10000; t <= 20000; t += 50) stats.on_level(t, true, -40.0f + static_cast<float>((t / 50) % 5));
  stats.on_level(20050, true, -12.5f);
  stats.on_level(20100, false, -99.0f);           // invalid: ignored for extremes
  stats.on_level(30000, true, -41.0f);            // a 10 s jump: not credited as measured time
  stats.on_level(30050, true, -80.0f);
  s = stats.summary(31000, 0);
  assert(s.have_level && s.level_max_dbfs == -12.5f && s.level_min_dbfs == -80.0f);
  assert(s.measured_s >= 10 && s.measured_s <= 10);   // 10 s of 50 ms steps; the gap added nothing
  assert(s.elapsed_s == 21);

  // Events: counted by kind; sweep records are not findings; the quiet stretch includes the one still running.
  stats.on_event(event(Kind::rf_rise, 15000));        // 5 s after start
  stats.on_event(event(Kind::audio_burst, 22000));    // 7 s later
  stats.on_event(event(Kind::manual_marker, 23000));
  stats.on_event(event(Kind::sweep_start, 24000));
  stats.on_event(event(Kind::sweep_stop, 25000));
  s = stats.summary(25000, 3);
  assert(s.events_rf == 1 && s.events_audio == 1 && s.events_marks == 1 && s.clips == 3);
  assert(s.longest_quiet_s == 7);
  s = stats.summary(60000, 3);                        // 37 s since the last event now beats every earlier stretch
  assert(s.longest_quiet_s == 37);

  // Channels: distinct only, zero ignored, bounded.
  stats.on_frequency(98700000u);
  stats.on_frequency(98700000u);
  stats.on_frequency(0);
  stats.on_frequency(121500000u);
  assert(stats.summary(60000, 0).channels == 2);
  for (uint32_t i = 0; i < 100; ++i) stats.on_frequency(100000000u + i * 200000u);
  assert(stats.summary(60000, 0).channels == SessionStats::kMaxChannels);

  stats.on_sweep_start();
  stats.on_sweep_start();
  assert(stats.summary(60000, 0).sweeps == 2);

  // A reset starts over from the new time.
  stats.reset(100000);
  s = stats.summary(101000, 0);
  assert(s.elapsed_s == 1 && s.events_rf == 0 && !s.have_level && s.channels == 0 && s.sweeps == 0 && s.longest_quiet_s == 1);
  std::puts("Dead Air session stats tests PASS");
  return 0;
}

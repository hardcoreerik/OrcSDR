// Control run: the three sensitivities count independently, gaps are not counted as observed time, a run ends by
// itself or early, and a rate is only reported once enough time was actually measured.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "dead_air_control.hpp"

using namespace orcsdr::dead_air;

namespace {
RfObservation sample(uint32_t ms, float level, bool valid = true, bool settled = true) {
  RfObservation s{};
  s.monotonic_ms = ms;
  s.frequency_hz = 98700000u;
  s.configuration_revision = 1;
  s.level_dbfs = level;
  s.valid = valid;
  s.settled = settled;
  return s;
}
// A flat floor with a burst of `rise_db` lasting `burst_ms`, repeated every `period_ms`.
void feed(ControlRun& run, uint32_t from_ms, uint32_t to_ms, float floor_db, float rise_db, uint32_t period_ms, uint32_t burst_ms) {
  for (uint32_t t = from_ms; t < to_ms; t += 50) {
    const uint32_t phase = (t - from_ms) % period_ms;
    const bool burst = rise_db > 0.0f && phase >= period_ms - burst_ms && t - from_ms > 2000;
    run.observe(sample(t, floor_db + (burst ? rise_db : 0.0f)));
  }
}
}  // namespace

int main() {
  ControlRun run;
  assert(!run.running() && !run.has_result());
  run.observe(sample(0, -40.0f));                 // ignored while idle
  assert(run.observed_ms() == 0);

  run.start(1000, 120000);
  assert(run.running() && !run.has_result() && run.count(Sensitivity::standard) == 0);
  // 6.5 dB bursts: above the Sensitive limit (+5), below Standard (+8) and Quiet (+12).
  feed(run, 1000, 61000, -40.0f, 6.5f, 10000, 500);
  assert(run.count(Sensitivity::sensitive) >= 4);
  assert(run.count(Sensitivity::standard) == 0 && run.count(Sensitivity::quiet) == 0);
  // 10 dB bursts: above Standard too, still below Quiet.
  feed(run, 61000, 121000, -40.0f, 10.0f, 10000, 500);
  assert(run.count(Sensitivity::standard) >= 4);
  assert(run.count(Sensitivity::quiet) == 0);
  assert(run.count(Sensitivity::sensitive) > run.count(Sensitivity::standard));
  assert(run.observed_ms() > 110000 && run.rate_available());
  assert(run.per_hour(Sensitivity::sensitive) > run.per_hour(Sensitivity::standard));
  assert(run.per_hour(Sensitivity::quiet) == 0.0f);
  run.tick(100000);
  assert(run.running());
  run.tick(121000);                               // its time is up: complete, and the counts stop changing
  assert(!run.running() && run.has_result() && run.complete());
  const uint32_t frozen = run.count(Sensitivity::sensitive);
  run.observe(sample(122000, -10.0f));
  assert(run.count(Sensitivity::sensitive) == frozen);
  assert(run.elapsed_ms(500000) == 120000);

  // Stopped early with too little measured time: kept, marked incomplete, and no rate is claimed.
  run.start(200000, 300000);
  feed(run, 200000, 230000, -40.0f, 10.0f, 5000, 500);
  run.stop(230000);
  assert(run.has_result() && !run.complete());
  assert(run.observed_ms() < ControlRun::kMinRateMs && !run.rate_available());
  assert(run.per_hour(Sensitivity::standard) == 0.0f);
  assert(run.elapsed_ms(900000) == 30000);

  // Gaps are not observed time: an unsettled stretch (a retune) adds nothing, and a jump over a gap adds nothing.
  run.start(400000, 300000);
  for (uint32_t t = 400000; t < 410000; t += 50) run.observe(sample(t, -40.0f));
  const uint32_t before = run.observed_ms();
  for (uint32_t t = 410000; t < 430000; t += 50) run.observe(sample(t, -40.0f, true, false));
  assert(run.observed_ms() == before);
  run.observe(sample(430050, -40.0f));            // first measured sample after the gap: no time credited for the gap
  assert(run.observed_ms() == before);
  run.observe(sample(430100, -40.0f));
  assert(run.observed_ms() == before + 50);

  // A run restarted from scratch forgets the earlier counts.
  run.start(500000);
  assert(run.running() && run.count(Sensitivity::sensitive) == 0 && run.observed_ms() == 0);
  assert(run.duration_ms() == ControlRun::kDefaultMs);
  std::puts("Dead Air control run tests PASS");
  return 0;
}

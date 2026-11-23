// Host tests for the environment event detector: warm-up, hold time, direction, sensitivity, gaps, re-baselining and
// the Event it builds for EventModel::append_measured.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "dead_air_envevents.hpp"

using namespace orcsdr::dead_air;

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
      std::exit(1);                                                              \
    }                                                                            \
  } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (tol))

namespace {

constexpr EnvChannel kT = EnvChannel::temperature, kH = EnvChannel::humidity, kP = EnvChannel::pressure;

// Feeds `seconds` samples at 1 Hz starting at *t (ms); returns how many findings fired and keeps the last one.
int feed(EnvEventDetector& d, EnvChannel ch, float value, int seconds, uint32_t* t, Sensitivity s, EnvFinding* last = nullptr) {
  int fired = 0;
  for (int i = 0; i < seconds; ++i) {
    *t += 1000;
    EnvFinding f;
    if (d.observe(ch, value, *t, s, &f)) { ++fired; if (last) *last = f; }
  }
  return fired;
}

void test_warmup_and_steady() {
  EnvEventDetector d;
  uint32_t t = 0;
  CHECK(!d.baseline_ready(kT));
  CHECK(feed(d, kT, 22.9f, 14, &t, Sensitivity::standard) == 0);
  CHECK(!d.baseline_ready(kT));
  CHECK(feed(d, kT, 22.9f, 60, &t, Sensitivity::standard) == 0);
  CHECK(d.baseline_ready(kT));
  NEAR(d.baseline(kT), 22.9, 0.001);
}

void test_step_fires_once_after_hold() {
  EnvEventDetector d;
  uint32_t t = 0;
  feed(d, kT, 22.9f, 30, &t, Sensitivity::standard);
  EnvFinding f;
  // +2.6 C (a breath on the sensor): the first sample over the limit starts the clock, the event fires 5 s later.
  CHECK(feed(d, kT, 25.5f, 5, &t, Sensitivity::standard) == 0);
  CHECK(feed(d, kT, 25.5f, 1, &t, Sensitivity::standard, &f) == 1);
  CHECK(f.channel == kT);
  NEAR(f.value, 25.5, 1e-4);
  NEAR(f.baseline, 22.9, 0.01);
  NEAR(f.threshold, 1.0, 1e-6);
  // Still away: no repeats.
  CHECK(feed(d, kT, 25.5f, 120, &t, Sensitivity::standard) == 0);
  // Back to normal re-arms the detector.
  CHECK(feed(d, kT, 22.9f, 10, &t, Sensitivity::standard) == 0);
  CHECK(feed(d, kT, 25.0f, 7, &t, Sensitivity::standard) == 1);
}

void test_short_blip_is_ignored() {
  EnvEventDetector d;
  uint32_t t = 0;
  feed(d, kT, 22.9f, 30, &t, Sensitivity::standard);
  CHECK(feed(d, kT, 25.5f, 4, &t, Sensitivity::standard) == 0);  // shorter than the 5 s hold
  CHECK(feed(d, kT, 22.9f, 30, &t, Sensitivity::standard) == 0);
}

void test_falling_departure() {
  EnvEventDetector d;
  uint32_t t = 0;
  feed(d, kT, 22.9f, 30, &t, Sensitivity::standard);
  EnvFinding f;
  CHECK(feed(d, kT, 21.4f, 8, &t, Sensitivity::standard, &f) == 1);
  CHECK(f.value < f.baseline);
}

void test_direction_change_restarts_hold() {
  EnvEventDetector d;
  uint32_t t = 0;
  feed(d, kT, 22.9f, 30, &t, Sensitivity::standard);
  CHECK(feed(d, kT, 24.2f, 4, &t, Sensitivity::standard) == 0);  // rising, 4 s in
  CHECK(feed(d, kT, 21.6f, 4, &t, Sensitivity::standard) == 0);  // now falling: the clock restarted
  CHECK(feed(d, kT, 21.6f, 3, &t, Sensitivity::standard) == 1);
}

void test_sensitivity_changes_the_limit() {
  for (int pass = 0; pass < 3; ++pass) {
    const Sensitivity s = pass == 0 ? Sensitivity::quiet : pass == 1 ? Sensitivity::standard : Sensitivity::sensitive;
    EnvEventDetector d;
    uint32_t t = 0;
    feed(d, kT, 22.9f, 30, &t, s);
    const int fired = feed(d, kT, 23.7f, 12, &t, s);  // +0.8 C
    CHECK(fired == (pass == 2 ? 1 : 0));
  }
  NEAR(env_threshold(kT, Sensitivity::quiet), 1.5, 1e-6);
  NEAR(env_threshold(kH, Sensitivity::standard), 4.0, 1e-6);
  NEAR(env_threshold(kP, Sensitivity::sensitive), 0.25, 1e-6);
}

void test_pressure_and_humidity() {
  EnvEventDetector d;
  uint32_t t = 0;
  feed(d, kP, 1001.9f, 30, &t, Sensitivity::standard);
  CHECK(feed(d, kP, 1001.92f, 20, &t, Sensitivity::standard) == 0);  // sensor noise
  CHECK(feed(d, kP, 1002.6f, 6, &t, Sensitivity::standard) == 1);     // +0.7 hPa held for 3 s
  uint32_t th = 0;  // each channel has its own timeline: a channel silent for over 5 s starts over
  feed(d, kH, 55.0f, 30, &th, Sensitivity::standard);
  CHECK(feed(d, kH, 61.0f, 7, &th, Sensitivity::standard) == 1);      // +6 %RH
  CHECK(env_kind(kP) == Kind::pressure_change && env_kind(kH) == Kind::humidity_change);
}

void test_slow_drift_is_absorbed() {
  EnvEventDetector d;
  uint32_t t = 0;
  feed(d, kT, 22.0f, 30, &t, Sensitivity::standard);
  int fired = 0;
  for (int i = 0; i < 3600; ++i) {  // +0.2 C per minute for an hour
    t += 1000;
    EnvFinding f;
    fired += d.observe(kT, 22.0f + 0.2f * static_cast<float>(i) / 60.0f, t, Sensitivity::standard, &f) ? 1 : 0;
  }
  CHECK(fired == 0);
}

void test_gap_and_bad_samples_start_over() {
  EnvEventDetector d;
  uint32_t t = 0;
  feed(d, kT, 22.9f, 30, &t, Sensitivity::standard);
  CHECK(d.baseline_ready(kT));
  t += 6000;  // the sensor went quiet for 6 s
  EnvFinding f;
  CHECK(!d.observe(kT, 30.0f, t, Sensitivity::standard, &f));
  CHECK(!d.baseline_ready(kT));  // re-learning from the first sample after the gap
  CHECK(feed(d, kT, 30.0f, 40, &t, Sensitivity::standard) == 0);  // 30 C is simply the new baseline
  EnvEventDetector e;
  t = 0;
  feed(e, kT, 22.9f, 30, &t, Sensitivity::standard);
  CHECK(!e.observe(kT, NAN, t + 1000, Sensitivity::standard, &f));
  CHECK(!e.baseline_ready(kT));
  // A repeated or older timestamp is not a new sample.
  EnvEventDetector g;
  t = 0;
  feed(g, kT, 22.9f, 30, &t, Sensitivity::standard);
  CHECK(!g.observe(kT, 40.0f, t, Sensitivity::standard, &f));
  CHECK(!g.observe(kT, 40.0f, t - 500, Sensitivity::standard, &f));
}

void test_long_departure_becomes_the_new_normal() {
  EnvEventDetector d;
  uint32_t t = 0;
  feed(d, kT, 22.9f, 30, &t, Sensitivity::standard);
  CHECK(feed(d, kT, 26.0f, 10, &t, Sensitivity::standard) == 1);
  CHECK(feed(d, kT, 26.0f, 330, &t, Sensitivity::standard) == 0);  // five minutes later it is re-learned silently
  NEAR(d.baseline(kT), 26.0, 0.01);
  EnvEventDetector e;
  t = 0;
  feed(e, kT, 22.9f, 30, &t, Sensitivity::standard);
  feed(e, kT, 26.0f, 400, &t, Sensitivity::standard);
  feed(e, kT, 26.0f, 30, &t, Sensitivity::standard);
  CHECK(feed(e, kT, 28.5f, 8, &t, Sensitivity::standard) == 1);  // armed again from the new baseline
}

void test_channels_are_independent() {
  EnvEventDetector d;
  uint32_t t = 0;
  feed(d, kT, 22.9f, 30, &t, Sensitivity::standard);
  CHECK(!d.baseline_ready(kH) && !d.baseline_ready(kP));
  d.reset(kT);
  CHECK(!d.baseline_ready(kT));
}

void test_event_is_accepted_by_the_model() {
  EnvFinding f{kT, 25.5f, 22.9f, 1.0f};
  const Event e = env_event(f, 123456, 1793000000u, Sensitivity::standard);
  CHECK(e.source == Source::environment && e.kind == Kind::temperature_change);
  CHECK(e.frequency_hz == 0 && e.unix_time == 1793000000u);
  EventModel m;
  CHECK(m.append_measured(e));
  CHECK(m.count() == 1 && m.newest(0)->kind == Kind::temperature_change);
  NEAR(m.newest(0)->value - m.newest(0)->reference, 2.6, 1e-4);
}

}  // namespace

int main() {
  test_warmup_and_steady();
  test_step_fires_once_after_hold();
  test_short_blip_is_ignored();
  test_falling_departure();
  test_direction_change_restarts_hold();
  test_sensitivity_changes_the_limit();
  test_pressure_and_humidity();
  test_slow_drift_is_absorbed();
  test_gap_and_bad_samples_start_over();
  test_long_departure_becomes_the_new_normal();
  test_channels_are_independent();
  test_event_is_accepted_by_the_model();
  std::printf("dead_air_envevents_tests: PASS\n");
  return 0;
}

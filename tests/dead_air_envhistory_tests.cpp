// Host tests for the environment history behind the Environment page charts: minute averaging, gaps, scaling, the
// 24-hour wrap and the per-column summary.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "dead_air_envhistory.hpp"

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

constexpr uint32_t kMin = EnvHistory::kBucketMs;

void test_minute_average() {
  static EnvHistory h;
  h.reset(1000);
  h.add(0, 20.0f, 1000);
  h.add(0, 22.0f, 20000);
  bool ok = false;
  NEAR(h.value(0, 0, &ok), 21.0, 0.011);
  CHECK(ok);
  CHECK(h.count() == 1);
  h.add(0, 25.0f, 1000 + kMin + 5);  // next minute starts its own mean
  NEAR(h.value(0, 0, &ok), 25.0, 0.011);
  NEAR(h.value(0, 1, &ok), 21.0, 0.011);
  CHECK(h.count() == 2);
}

void test_scaling_and_channels() {
  static EnvHistory h;
  h.reset(0);
  h.add(0, 20.89f, 10);
  h.add(1, 54.39f, 10);
  h.add(2, 999.79f, 10);
  CHECK(h.at(0, 0) == 2089);
  CHECK(h.at(1, 0) == 5439);
  CHECK(h.at(2, 0) == 9998);
  bool ok = false;
  NEAR(h.value(2, 0, &ok), 999.8, 0.01);
  h.add(2, -5000.0f, 20);  // outside the int16 range: refused, never wrapped
  CHECK(h.at(2, 0) == 9998);
  // A refused value stamped in a LATER minute must not move the window: the valid sample for the current minute that
  // arrives next is still accepted, and no phantom minute appears.
  const uint32_t seq = h.seq();
  h.add(2, 9000.0f, 3 * kMin);  // 90000 scaled: out of range
  CHECK(h.count() == 1 && h.seq() == seq);
  h.add(2, 1000.5f, 30);
  CHECK(h.count() == 1);  // still the current minute, now the mean of the two valid samples
  NEAR(h.value(2, 0, &ok), (999.79 + 1000.5) / 2.0, 0.06);
}

void test_before_reset_and_clock_wrap() {
  static EnvHistory h;
  h.reset(100000);
  h.add(0, 20.0f, 50000);          // stamped before reset(): ignored, not folded into minute 0
  CHECK(h.at(0, 0) == kHistGap);
  h.add(0, 20.0f, 100010);
  CHECK(h.at(0, 0) == 2000);
  // A session that runs past the 32-bit millisecond wrap (49.7 days) keeps recording.
  static EnvHistory w;
  const uint32_t start = 0xFFFFFFFFu - 90000u;  // 90 s before the clock wraps
  w.reset(start);
  for (uint32_t m = 0; m < 5; ++m) w.add(0, 10.0f + static_cast<float>(m), start + m * kMin + 100u);  // the 2nd and later sit after the wrap
  CHECK(w.count() == 5);
  for (uint32_t m = 0; m < 5; ++m) CHECK(w.at(0, 4 - m) == static_cast<int16_t>((10 + m) * 100));
  w.add(0, 99.0f, start + 5 * kMin + 100u);
  CHECK(w.count() == 6 && w.at(0, 0) == 9900);
}

// A session whose elapsed time passes 2^32 ms (49.7 days) since reset(), sampled regularly the whole way: history must keep
// accepting samples and the newest value must be the one just added, before and after the crossing.
void test_session_longer_than_the_clock_wrap() {
  static EnvHistory h;
  h.reset(123456u);
  uint32_t clock = 123456u;
  uint64_t elapsed = 0;
  const uint32_t step = 3600000u;  // one sample an hour
  int crossed_at = -1;
  for (int i = 0; i < 1300; ++i) {  // 54 days
    clock += step;
    elapsed += step;
    if (crossed_at < 0 && elapsed > 0xFFFFFFFFull) crossed_at = i;
    h.add(0, 10.0f + static_cast<float>(i % 50), clock);
    CHECK(h.at(0, 0) == static_cast<int16_t>((10 + i % 50) * 100));  // the sample just added is the newest bucket
  }
  CHECK(crossed_at > 1000 && crossed_at < 1300);  // the run really did pass 2^32 elapsed milliseconds
  CHECK(h.count() == EnvHistory::kBuckets);
  h.tick(clock + 5 * kMin);                        // and a silent stretch afterwards still opens gaps
  CHECK(h.at(0, 0) == kHistGap && h.at(0, 5) != kHistGap);
}

void test_tick_turns_silence_into_gaps() {
  static EnvHistory h;
  h.reset(0);
  h.add(0, 20.0f, 10);
  const uint32_t seq = h.seq();
  h.tick(kMin - 1);                // still the same minute: nothing happens
  CHECK(h.count() == 1 && h.seq() == seq);
  h.tick(5 * kMin + 1);            // five silent minutes: the newest column is a gap, not the last value
  CHECK(h.count() == 6 && h.seq() != seq);
  CHECK(h.at(0, 0) == kHistGap && h.at(0, 4) == kHistGap && h.at(0, 5) == 2000);
  h.add(0, 21.0f, 5 * kMin + 5);   // the sensor comes back into the minute the tick opened
  CHECK(h.at(0, 0) == 2100 && h.count() == 6);
  EnvHistory unstarted;
  unstarted.tick(1000);            // before reset(): harmless
  CHECK(unstarted.count() == 0);
}

void test_gaps_are_not_carried_over() {
  static EnvHistory h;
  h.reset(0);
  h.add(0, 20.0f, 0);
  h.add(0, 21.0f, 3 * kMin + 1);  // two minutes with no sample in between
  CHECK(h.count() == 4);
  CHECK(h.at(0, 0) == 2100);
  CHECK(h.at(0, 1) == kHistGap);
  CHECK(h.at(0, 2) == kHistGap);
  CHECK(h.at(0, 3) == 2000);
  CHECK(h.at(1, 0) == kHistGap);  // a channel that never reported stays a gap
  CHECK(h.at(0, 4) == kHistGap);  // beyond what has been recorded
}

void test_bad_input_and_late_samples() {
  static EnvHistory h;
  h.reset(0);
  h.add(0, NAN, 10);
  h.add(0, INFINITY, 10);
  h.add(7, 1.0f, 10);
  CHECK(h.at(0, 0) == kHistGap);
  h.add(0, 20.0f, 2 * kMin);
  const uint32_t seq = h.seq();
  h.add(0, 99.0f, 10);  // a minute that is already closed
  CHECK(h.seq() == seq);
  CHECK(h.at(0, 0) == 2000);
}

void test_wraps_after_24_hours() {
  static EnvHistory h;
  h.reset(0);
  for (uint32_t m = 0; m < EnvHistory::kBuckets + 90; ++m) h.add(0, static_cast<float>(m % 50), m * kMin);
  CHECK(h.count() == EnvHistory::kBuckets);
  const uint32_t newest = EnvHistory::kBuckets + 89;
  CHECK(h.at(0, 0) == static_cast<int16_t>((newest % 50) * 100));
  CHECK(h.at(0, EnvHistory::kBuckets - 1) == static_cast<int16_t>(((newest - (EnvHistory::kBuckets - 1)) % 50) * 100));
  CHECK(h.at(0, EnvHistory::kBuckets) == kHistGap);
  // A silence longer than the whole history leaves only gaps behind the newest value.
  h.add(0, 5.0f, (newest + 3000) * kMin);
  CHECK(h.at(0, 0) == 500);
  CHECK(h.at(0, 1) == kHistGap);
  CHECK(h.at(0, EnvHistory::kBuckets - 1) == kHistGap);
}

void test_columns() {
  static EnvHistory h;
  h.reset(0);
  for (uint32_t m = 0; m < 60; ++m) h.add(0, static_cast<float>(m), m * kMin);
  EnvColumn cols[6];
  env_columns(h, 0, 60, 6, cols);
  CHECK(cols[5].valid);
  NEAR(cols[5].value, 54.5, 0.01);  // newest ten minutes: 50..59
  NEAR(cols[0].value, 4.5, 0.01);   // oldest ten minutes: 0..9
  EnvColumn wide[120];              // more columns than buckets: every column still gets a bucket
  env_columns(h, 0, 60, 120, wide);
  CHECK(wide[119].valid);
  NEAR(wide[119].value, 59.0, 0.01);
  // A column that contains a missing minute is a gap, so a 24 H line cannot hide a short outage.
  static EnvHistory g;
  g.reset(0);
  for (uint32_t m = 0; m < 60; ++m)
    if (m != 30) g.add(0, 10.0f, m * 60000u + 10u);
  EnvColumn three[20];             // three buckets per column
  env_columns(g, 0, 60, 20, three);
  int invalid = 0;
  for (int i = 0; i < 20; ++i) invalid += three[i].valid ? 0 : 1;
  CHECK(invalid == 1);
  CHECK(!three[10].valid);         // minutes 30..32 from the oldest: offsets 29..27 -> the column holding minute 30
  CHECK(three[9].valid && three[11].valid);
  EnvColumn empty[4];
  env_columns(h, 1, 60, 4, empty);  // a channel with no readings is invalid, not zero
  CHECK(!empty[0].valid && !empty[3].valid);
  env_columns(h, 0, 5000, 4, cols);  // a span past the history is clamped
}

}  // namespace

int main() {
  test_minute_average();
  test_scaling_and_channels();
  test_before_reset_and_clock_wrap();
  test_session_longer_than_the_clock_wrap();
  test_tick_turns_silence_into_gaps();
  test_gaps_are_not_carried_over();
  test_bad_input_and_late_samples();
  test_wraps_after_24_hours();
  test_columns();
  std::printf("dead_air_envhistory_tests: PASS\n");
  return 0;
}

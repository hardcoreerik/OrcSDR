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
  EnvColumn empty[4];
  env_columns(h, 1, 60, 4, empty);  // a channel with no readings is invalid, not zero
  CHECK(!empty[0].valid && !empty[3].valid);
  env_columns(h, 0, 5000, 4, cols);  // a span past the history is clamped
}

}  // namespace

int main() {
  test_minute_average();
  test_scaling_and_channels();
  test_gaps_are_not_carried_over();
  test_bad_input_and_late_samples();
  test_wraps_after_24_hours();
  test_columns();
  std::printf("dead_air_envhistory_tests: PASS\n");
  return 0;
}

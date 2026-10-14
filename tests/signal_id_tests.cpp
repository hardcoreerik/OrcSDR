#include "../ui/signal_id.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
using orcsdr::signal_id::Tracker;
using orcsdr::signal_id::Signal;

int main() {
  Tracker detector;
  std::vector<float> bins(256, -80.0f);
  Signal out[8];
  const uint32_t center = 145500000, span = 256000;
  auto observe = [&](uint32_t ms) { return detector.observe(bins.data(), bins.size(), center, span, ms); };
  // A flat baseline doesn't invent RF events.
  for (int i=1; i<=10; ++i) assert(observe(i*100) == 0);
  // Single transient does not appear (3-of-8).
  bins[64] = -55;
  assert(observe(1100) == 0);
  bins[64] = -80;
  assert(observe(1200) == 0);
  assert(detector.visible(out, 8, 1200) == 0);
  // Repeated narrow signal becomes visible and carries physical units.
  bins[64] = -55;
  assert(observe(1300) == 0);
  assert(observe(1400) == 1);
  assert(detector.visible(out, 8, 1400) == 1);
  assert(std::abs(static_cast<int64_t>(out[0].frequency_hz) - 145436000) < 1500);
  assert(out[0].bandwidth_hz >= 1000);
  assert(out[0].above_floor_db > 20.0f);
  // Second distinct signal has an independent track.
  bins[180] = -55;
  assert(observe(1500) == 1);
  assert(observe(1600) == 1);
  assert(observe(1700) == 2);
  // Retune removes stale markers instead of relabeling old RF.
  assert(detector.observe(bins.data(), bins.size(), center+1000000, span, 1800) == 0);
  assert(detector.visible(out, 8, 1800) == 0);
  // Stale or missing frames expire.
  detector.reset();
  bins.assign(256, -80.0f); bins[100] = -60;
  for (int i=1;i<=4;++i) detector.observe(bins.data(), bins.size(), center, span, i*100);
  assert(detector.visible(out, 8, 400) == 1);
  bins[100] = -80;
  for (int i=5;i<=12;++i) detector.observe(bins.data(), bins.size(), center, span, i*100);
  assert(detector.visible(out, 8, 1200) == 0);
  // No spurious output on invalid/stale input or nonfinite measurements.
  assert(detector.observe(nullptr, 256, center, span, 1300) == 0);
  bins.assign(256, std::numeric_limits<float>::quiet_NaN());
  assert(observe(1400) == 0);
  // Huge span / center safely maps without unsigned wraparound.
  bins.assign(256, -80.0f); bins[200] = -50;
  for (int i=1;i<=3;++i) detector.observe(bins.data(), bins.size(), 1700000000u, 2400000u, 1500+i*100);
  assert(detector.visible(out, 8, 1800) == 1);
  return 0;
}

// The orb animation: idle glow follows a measurement, bursts scale with the measured rise, are deterministic per
// event, stay inside the box, and end by themselves.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "dead_air_fx.hpp"

using namespace orcsdr::dead_air;

int main() {
  Fx fx;
  // Nothing measured: dim, still, no burst.
  fx.set_idle(false, 0.9f);
  FxFrame idle = fx.frame(1000);
  assert(!idle.burst && !idle.live && idle.glow < 0.2f);
  assert(fx.frame(1000).core == fx.frame(2500).core);   // still, not breathing
  // Measured: glow rises with how close the level is to the limit.
  fx.set_idle(true, 0.0f);
  const float far_glow = fx.frame(1000).glow;
  fx.set_idle(true, 1.0f);
  const float near_glow = fx.frame(1000).glow;
  assert(near_glow > far_glow + 0.3f && fx.frame(1000).live);
  assert(fx.frame(1000).core > 0.0f && fx.frame(1000).core < 1.0f);

  // A strong RF rise bursts with more rings and sparks than a weak one.
  Fx strong, weak, mark;
  strong.trigger(FxKind::rf, 18.0f, 7, 5000);
  weak.trigger(FxKind::rf, 5.0f, 7, 5000);
  mark.trigger(FxKind::mark, 0.0f, 3, 5000);
  const FxFrame s0 = strong.frame(5100), w0 = weak.frame(5100), m0 = mark.frame(5100);
  assert(s0.burst && s0.kind == FxKind::rf && w0.burst && m0.kind == FxKind::mark);
  assert(s0.ring_count == 3 && w0.ring_count == 1 && m0.ring_count == 2);
  assert(s0.spark_count > w0.spark_count && s0.spark_count <= kFxSparks && w0.spark_count >= 4);
  assert(s0.core > fx.frame(5100).core);                // the flash is bigger than the idle orb
  assert(fx_color(FxKind::rf) != fx_color(FxKind::audio) && fx_color(FxKind::audio) != fx_color(FxKind::mark));

  // Deterministic: the same event sparks the same way; a different event does not.
  Fx again, other;
  again.trigger(FxKind::rf, 18.0f, 7, 9000);
  other.trigger(FxKind::rf, 18.0f, 8, 9000);
  const FxFrame a = again.frame(9500), o = other.frame(9500), s = strong.frame(5500);
  for (size_t i = 0; i < s.spark_count; ++i) assert(a.spark_x[i] == s.spark_x[i] && a.spark_y[i] == s.spark_y[i]);
  bool differs = false;
  for (size_t i = 0; i < a.spark_count; ++i) differs = differs || a.spark_x[i] != o.spark_x[i];
  assert(differs);

  // Everything stays inside the box for the whole burst, and the burst ends.
  for (uint32_t t = 5000; t < 5000 + Fx::kBurstMs + 400; t += 40) {
    const FxFrame f = strong.frame(t);
    assert(f.core >= 0.0f && f.core <= 1.0f);
    for (size_t i = 0; i < f.spark_count; ++i) {
      assert(std::fabs(f.spark_x[i]) <= 1.0f && std::fabs(f.spark_y[i]) <= 1.0f);
      assert(f.spark_a[i] >= 0.0f && f.spark_a[i] <= 1.0f);
    }
    for (size_t i = 0; i < f.ring_count; ++i) assert(f.ring_r[i] >= 0.0f && f.ring_r[i] <= 1.0f && f.ring_a[i] >= 0.0f && f.ring_a[i] <= 1.0f);
  }
  assert(strong.animating(5000 + Fx::kBurstMs - 1) && !strong.animating(5000 + Fx::kBurstMs));
  assert(!strong.frame(5000 + Fx::kBurstMs + 1).burst);
  // Sparks fade out as the burst ends.
  assert(strong.frame(5000 + Fx::kBurstMs - 10).spark_a[0] < 0.1f);

  // A new event restarts the animation; kind none and a non-finite rise are safe.
  strong.trigger(FxKind::audio, 9.0f, 11, 8000);
  assert(strong.frame(8100).kind == FxKind::audio && strong.animating(8100));
  strong.trigger(FxKind::none, 9.0f, 12, 8200);
  assert(strong.frame(8300).kind == FxKind::audio);
  Fx nan;
  nan.trigger(FxKind::rf, NAN, 1, 100);
  assert(nan.frame(200).spark_count == 6);

  // Colour blending.
  assert(blend565(0xf800, 0x0000, 1.0f) == 0xf800 && blend565(0xf800, 0x0000, 0.0f) == 0x0000);
  assert(blend565(0xffff, 0x0000, 2.0f) == 0xffff && blend565(0xffff, 0x0000, -1.0f) == 0x0000);
  const uint16_t half = blend565(0xf800, 0x0000, 0.5f);
  assert(((half >> 11) & 0x1f) >= 15 && ((half >> 11) & 0x1f) <= 16 && (half & 0x7ff) == 0);
  std::puts("Dead Air orb tests PASS");
  return 0;
}

#include "dead_air_fx.hpp"

#include <algorithm>
#include <cmath>

namespace orcsdr::dead_air {
namespace {
float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }
uint32_t lcg(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state >> 8;
}
}  // namespace

uint16_t fx_color(FxKind kind) {
  switch (kind) {
    case FxKind::rf: return 0xf800;     // red
    case FxKind::audio: return 0xfd20;  // orange
    case FxKind::mark: return 0xa15f;   // violet
    default: return 0x2e7f;             // cyan: idle
  }
}

uint16_t blend565(uint16_t fg, uint16_t bg, float alpha) {
  const float a = clamp01(alpha);
  const auto mix = [a](int f, int b) { return static_cast<int>(std::lround(static_cast<float>(b) + (static_cast<float>(f - b)) * a)); };
  const int r = mix((fg >> 11) & 0x1f, (bg >> 11) & 0x1f);
  const int g = mix((fg >> 5) & 0x3f, (bg >> 5) & 0x3f);
  const int b = mix(fg & 0x1f, bg & 0x1f);
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

void Fx::reset() { *this = Fx(); }

void Fx::trigger(FxKind kind, float rise_db, uint32_t sequence, uint32_t now_ms) {
  if (kind == FxKind::none) return;
  kind_ = kind;
  sequence_ = sequence;
  start_ms_ = now_ms;
  burst_ = true;
  if (kind == FxKind::mark || !std::isfinite(rise_db)) {
    intensity_ = 0.45f;
    sparks_ = 6;
    rings_ = 2;
  } else {
    intensity_ = std::min(1.0f, std::max(0.2f, rise_db / 20.0f));
    sparks_ = static_cast<uint8_t>(std::min<float>(kFxSparks, std::max(4.0f, 4.0f + rise_db * 0.8f)));
    rings_ = rise_db >= 12.0f ? 3 : rise_db >= 6.0f ? 2 : 1;
  }
}

void Fx::set_idle(bool live, float proximity) {
  live_ = live;
  proximity_ = live ? clamp01(proximity) : 0.0f;
}

bool Fx::animating(uint32_t now_ms) const { return burst_ && now_ms - start_ms_ < kBurstMs; }

FxFrame Fx::frame(uint32_t now_ms) const {
  FxFrame f;
  f.live = live_;
  // Idle: a slow breath while a level is measured, bigger and brighter the closer the level is to the event limit.
  const float breath = live_ ? 0.5f + 0.5f * std::sin(static_cast<float>(now_ms % 6283u) / 1000.0f * 1.4f) : 0.0f;
  f.core = live_ ? 0.26f + 0.06f * breath + 0.22f * proximity_ : 0.22f;
  f.glow = live_ ? 0.25f + 0.2f * breath + 0.55f * proximity_ : 0.08f;
  if (!animating(now_ms)) return f;

  const float p = static_cast<float>(now_ms - start_ms_) / static_cast<float>(kBurstMs);  // 0..1
  const float fade = (1.0f - p) * (1.0f - p);
  f.burst = true;
  f.kind = kind_;
  f.core += fade * (0.35f + 0.25f * intensity_);
  f.glow = clamp01(f.glow + fade * 0.9f);
  f.ring_count = rings_;
  for (uint8_t i = 0; i < rings_ && i < kFxRings; ++i) {
    const float q = (p - 0.16f * static_cast<float>(i)) / 0.7f;  // each ring starts a little later
    if (q <= 0.0f || q >= 1.0f) { f.ring_a[i] = 0.0f; f.ring_r[i] = 0.0f; continue; }
    const float eased = 1.0f - (1.0f - q) * (1.0f - q);
    f.ring_r[i] = 0.3f + 0.7f * eased;
    f.ring_a[i] = (1.0f - q) * (0.55f + 0.4f * intensity_);
  }
  f.spark_count = std::min<uint8_t>(sparks_, kFxSparks);
  uint32_t state = sequence_ * 2654435761u + 12345u;  // the same event always sparks the same way
  const float travel = 1.0f - (1.0f - p) * (1.0f - p);
  for (uint8_t i = 0; i < f.spark_count; ++i) {
    const float angle = static_cast<float>(lcg(state) % 6283u) / 1000.0f;
    const float speed = 0.45f + 0.5f * static_cast<float>(lcg(state) % 1000u) / 1000.0f;
    const float dist = 0.2f + speed * 0.8f * travel;
    f.spark_x[i] = std::cos(angle) * dist;
    f.spark_y[i] = std::sin(angle) * dist;
    f.spark_a[i] = clamp01(1.0f - p * 1.15f);
  }
  return f;
}

}  // namespace orcsdr::dead_air

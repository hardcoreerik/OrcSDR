#include "signal_id.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace orcsdr::signal_id {
namespace {
constexpr float kDetectDb = 9.0f;
constexpr float kEdgeDb = 5.0f;
constexpr size_t kFloorSamples = 64;
constexpr uint32_t kMaxFrameGapMs = 1600;
constexpr uint32_t kVisibleHoldMs = 600;

unsigned hit_count(uint8_t history) {
  unsigned n = 0;
  while (history) { n += history & 1u; history >>= 1u; }
  return n;
}

// Quantile sampling avoids using an already raised spectrum mean as the floor
// when several strong signals are present. 64 floats stay on the stack.
float estimate_floor(const float* db, size_t n) {
  float samples[kFloorSamples]{};
  size_t used = 0;
  for (size_t i = 0; i < kFloorSamples && i < n; ++i) {
    const size_t index = (i * (n - 1)) / (kFloorSamples - 1);
    if (!std::isfinite(db[index])) continue;
    const float value = db[index];
    size_t pos = used++;
    while (pos > 0 && samples[pos - 1] > value) {
      samples[pos] = samples[pos - 1];
      --pos;
    }
    samples[pos] = value;
  }
  return used >= 8 ? samples[used / 4] : std::numeric_limits<float>::quiet_NaN();
}

uint32_t pixel_frequency(size_t pixel, size_t count, uint32_t center, uint32_t span) {
  const double offset = (static_cast<double>(pixel) / static_cast<double>(count - 1) - 0.5) * span;
  const double hz = static_cast<double>(center) + offset;
  if (hz <= 0.0) return 0;
  if (hz >= 4294967295.0) return UINT32_MAX;
  return static_cast<uint32_t>(std::llround(hz));
}

uint32_t delta(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }
}  // namespace

void Tracker::reset() {
  for (auto& track : tracks_) track = {};
  center_hz_ = span_hz_ = last_ms_ = 0;
  count_ = 0;
}

size_t Tracker::observe(const float* db, size_t count, uint32_t center_hz,
                        uint32_t span_hz, uint32_t now_ms) {
  if (!db || count < 32 || count > 4096 || center_hz == 0 || span_hz == 0) {
    reset();
    return 0;
  }
  if (count_ != count || center_hz_ != center_hz || span_hz_ != span_hz ||
      (last_ms_ != 0 && now_ms - last_ms_ > kMaxFrameGapMs)) {
    reset();
  }
  count_ = count;
  center_hz_ = center_hz;
  span_hz_ = span_hz;
  last_ms_ = now_ms;
  const float floor = estimate_floor(db, count);
  if (!std::isfinite(floor)) { reset(); return 0; }

  for (auto& t : tracks_) {
    t.history = static_cast<uint8_t>(t.history << 1u);
    if (t.active && (now_ms - t.last_seen_ms > kMaxFrameGapMs || t.history == 0)) t = {};
  }

  // Peak candidates are contiguous regions above relative floor. The hysteresis
  // region is bounded; disconnected candidates remain distinct.
  const size_t margin = std::max<size_t>(2, count / 100);
  size_t i = margin;
  while (i + margin < count) {
    if (!std::isfinite(db[i]) || db[i] < floor + kDetectDb) { ++i; continue; }
    size_t lo = i, hi = i, peak = i;
    // Walk both sides to the relaxed edge threshold.
    while (lo > margin && std::isfinite(db[lo - 1]) && db[lo - 1] >= floor + kEdgeDb) --lo;
    while (hi + margin + 1 < count && std::isfinite(db[hi + 1]) && db[hi + 1] >= floor + kEdgeDb) ++hi;
    for (size_t p = lo; p <= hi; ++p) if (db[p] > db[peak]) peak = p;
    i = hi + 1;

    const uint32_t hz = pixel_frequency(peak, count, center_hz, span_hz);
    const uint32_t bw = std::max<uint32_t>(1, static_cast<uint32_t>(
        (static_cast<uint64_t>(hi - lo + 1) * span_hz) / count));
    const uint32_t bin_hz = std::max<uint32_t>(1, span_hz / static_cast<uint32_t>(count));
    size_t best = kMaxTracks;
    uint32_t best_distance = UINT32_MAX;
    for (size_t t = 0; t < kMaxTracks; ++t) {
      if (!tracks_[t].active || (tracks_[t].history & 1u)) continue;
      const uint32_t distance = delta(tracks_[t].frequency_hz, hz);
      const uint32_t tolerance = std::max<uint32_t>(bin_hz * 4u,
          std::max(tracks_[t].bandwidth_hz, bw) / 2u);
      if (distance <= tolerance && distance < best_distance) {
        best = t;
        best_distance = distance;
      }
    }
    if (best == kMaxTracks) {
      for (size_t t = 0; t < kMaxTracks; ++t) {
        if (!tracks_[t].active) { best = t; break; }
      }
    }
    if (best == kMaxTracks) continue;  // bounded. Never block the IQ/display path.
    Signal& track = tracks_[best];
    track.active = true;
    track.history |= 1u;
    track.frequency_hz = hz;
    track.bandwidth_hz = bw;
    track.above_floor_db = db[peak] - floor;
    track.last_seen_ms = now_ms;
  }
  return visible(nullptr, 0, now_ms);
}

size_t Tracker::visible(Signal* out, size_t capacity, uint32_t now_ms) const {
  size_t found = 0;
  for (const auto& t : tracks_) {
    if (!t.active || hit_count(t.history) < 3 || now_ms - t.last_seen_ms > kVisibleHoldMs)
      continue;
    if (out && found < capacity) out[found] = t;
    ++found;
  }
  return found;
}

}  // namespace orcsdr::signal_id

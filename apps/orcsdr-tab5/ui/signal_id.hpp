#pragma once

#include <cstddef>
#include <cstdint>

// Host-testable signal activity detection. This is NOT a trained modulation classifier.
// Input is a linear-frequency, already computed spectrum in relative dB units.
// Inspired by OrcNode's baseline/track separation and K-of-N evidence gates;
// the Tab5 Home receives one continuously observed span, not OrcNode scan hops.
namespace orcsdr::signal_id {

constexpr size_t kMaxTracks = 8;

struct Signal {
  uint32_t frequency_hz = 0;
  uint32_t bandwidth_hz = 0;
  float above_floor_db = 0.0f;  // relative spectrum level, not calibrated dBm
  uint8_t history = 0;         // last 8 observations, bit 0 = most recent
  uint32_t last_seen_ms = 0;
  bool active = false;
};

class Tracker {
 public:
  void reset();
  // Returns 0 on invalid input. Each call advances history by one observation.
  // Tracks are reset when the frequency, span, or pixel count changes.
  size_t observe(const float* db, size_t count, uint32_t center_hz,
                 uint32_t span_hz, uint32_t now_ms);
  // Only stable signals (3 of last 8 frames, present recently) are exposed.
  size_t visible(Signal* out, size_t capacity, uint32_t now_ms) const;

 private:
  Signal tracks_[kMaxTracks]{};
  uint32_t center_hz_ = 0;
  uint32_t span_hz_ = 0;
  uint32_t last_ms_ = 0;
  size_t count_ = 0;
};

}  // namespace orcsdr::signal_id

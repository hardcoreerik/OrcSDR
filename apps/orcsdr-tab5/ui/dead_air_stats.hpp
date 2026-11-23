#pragma once
// Session facts for the Overview's session card: what was measured and logged since Dead Air was opened. Everything is
// a count or an extreme of something the receiver actually measured; nothing is scored or interpreted. Pure logic,
// host-tested.
#include <cstdint>

#include "dead_air_model.hpp"

namespace orcsdr::dead_air {

class SessionStats {
 public:
  static constexpr size_t kMaxChannels = 32;

  void reset(uint32_t now_ms);
  // Call with every RF observation. `valid` is true only for a settled, measured level.
  void on_level(uint32_t now_ms, bool valid, float dbfs);
  void on_event(const Event& event);  // RF, audio and marks only; sweep records are ignored
  void on_frequency(uint32_t hz);     // distinct tuned frequencies, counted up to kMaxChannels
  void on_sweep_start();
  SessionSummary summary(uint32_t now_ms, uint32_t clips_saved) const;

 private:
  uint32_t start_ms_ = 0, last_event_ms_ = 0, longest_quiet_ms_ = 0;
  uint32_t measured_ms_ = 0, last_valid_ms_ = 0;
  bool have_last_valid_ = false, have_level_ = false;
  float max_dbfs_ = 0.0f, min_dbfs_ = 0.0f;
  uint32_t rf_ = 0, audio_ = 0, marks_ = 0, sweeps_ = 0;
  uint32_t channels_[kMaxChannels]{};
  uint8_t channel_count_ = 0;
};

}  // namespace orcsdr::dead_air

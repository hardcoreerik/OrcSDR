#include "dead_air_stats.hpp"

#include <algorithm>
#include <cmath>

namespace orcsdr::dead_air {

void SessionStats::reset(uint32_t now_ms) { *this = SessionStats(); start_ms_ = last_event_ms_ = now_ms; }

void SessionStats::on_level(uint32_t now_ms, bool valid, float dbfs) {
  if (!valid || !std::isfinite(dbfs)) { have_last_valid_ = false; return; }
  if (have_last_valid_) {
    const uint32_t delta = now_ms - last_valid_ms_;
    if (delta <= 500u) measured_ms_ += delta;  // a longer jump is a gap, not time spent measuring
  }
  last_valid_ms_ = now_ms;
  have_last_valid_ = true;
  if (!have_level_) { max_dbfs_ = min_dbfs_ = dbfs; have_level_ = true; return; }
  max_dbfs_ = std::max(max_dbfs_, dbfs);
  min_dbfs_ = std::min(min_dbfs_, dbfs);
}

void SessionStats::on_event(const Event& event) {
  switch (event.kind) {
    case Kind::rf_rise: ++rf_; break;
    case Kind::audio_burst: ++audio_; break;
    case Kind::manual_marker: ++marks_; break;
    default: return;
  }
  if (event.monotonic_ms - last_event_ms_ < 0x80000000u)  // ignore a timestamp from before the last one
    longest_quiet_ms_ = std::max(longest_quiet_ms_, event.monotonic_ms - last_event_ms_);
  last_event_ms_ = event.monotonic_ms;
}

void SessionStats::on_frequency(uint32_t hz) {
  if (hz == 0) return;
  for (uint8_t i = 0; i < channel_count_; ++i)
    if (channels_[i] == hz) return;
  if (channel_count_ < kMaxChannels) channels_[channel_count_++] = hz;
}

void SessionStats::on_sweep_start() { ++sweeps_; }

SessionSummary SessionStats::summary(uint32_t now_ms, uint32_t clips_saved) const {
  SessionSummary s;
  s.elapsed_s = (now_ms - start_ms_) / 1000u;
  s.events_rf = rf_;
  s.events_audio = audio_;
  s.events_marks = marks_;
  s.measured_s = measured_ms_ / 1000u;
  s.have_level = have_level_;
  s.level_max_dbfs = max_dbfs_;
  s.level_min_dbfs = min_dbfs_;
  // The stretch still in progress counts as well as the ones that ended with an event.
  s.longest_quiet_s = std::max(longest_quiet_ms_, now_ms - last_event_ms_) / 1000u;
  s.channels = channel_count_;
  s.sweeps = sweeps_;
  s.clips = clips_saved;
  return s;
}

}  // namespace orcsdr::dead_air

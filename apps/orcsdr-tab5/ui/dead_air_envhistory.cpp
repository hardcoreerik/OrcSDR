#include "dead_air_envhistory.hpp"
#include <cmath>

namespace orcsdr::dead_air {

constexpr float EnvHistory::kScale[EnvHistory::kChannels];

void EnvHistory::reset(uint32_t now_ms) {
  for (size_t c = 0; c < kChannels; ++c) {
    for (size_t i = 0; i < kBuckets; ++i) data_[c][i] = kHistGap;
    sum_[c] = 0.0f;
    n_[c] = 0;
  }
  elapsed_ms_ = 0;
  last_clock_ms_ = now_ms;
  newest_bucket_ = 0;
  count_ = 1;
  started_ = true;
  ++seq_;
}

// Elapsed time is the running sum of signed 32-bit steps of the clock, so it survives the clock's wrap and tolerates a
// sample a little older than the newest clock reading (up to about 24 days either way between two readings).
void EnvHistory::observe_clock(uint32_t now_ms) {
  const int32_t step = static_cast<int32_t>(now_ms - last_clock_ms_);
  if (step > 0) {
    elapsed_ms_ += static_cast<uint32_t>(step);
    last_clock_ms_ = now_ms;
  }
}

bool EnvHistory::locate(uint32_t now_ms, uint64_t* bucket) const {
  const int32_t step = static_cast<int32_t>(now_ms - last_clock_ms_);
  if (step < 0 && static_cast<uint64_t>(-static_cast<int64_t>(step)) > elapsed_ms_) return false;  // before reset()
  const uint64_t at = step >= 0 ? elapsed_ms_ + static_cast<uint32_t>(step) : elapsed_ms_ - static_cast<uint64_t>(-static_cast<int64_t>(step));
  *bucket = at / kBucketMs;
  return true;
}

void EnvHistory::advance_to(uint64_t bucket) {
  const uint64_t steps = bucket - newest_bucket_;
  const uint64_t fill = steps < kBuckets ? steps : static_cast<uint64_t>(kBuckets);
  for (uint64_t k = 1; k <= fill; ++k) {  // minutes with no sample are gaps, never carried-over values
    const size_t slot = static_cast<size_t>((bucket - fill + k) % kBuckets);
    for (size_t c = 0; c < kChannels; ++c) data_[c][slot] = kHistGap;
  }
  for (size_t c = 0; c < kChannels; ++c) { sum_[c] = 0.0f; n_[c] = 0; }
  count_ = steps >= kBuckets || count_ + steps > kBuckets ? kBuckets : count_ + static_cast<size_t>(steps);
  newest_bucket_ = bucket;
  ++seq_;
}

void EnvHistory::tick(uint32_t now_ms) {
  if (!started_) return;
  observe_clock(now_ms);
  uint64_t bucket = 0;
  if (locate(now_ms, &bucket) && bucket > newest_bucket_) advance_to(bucket);
}

void EnvHistory::add(size_t channel, float value, uint32_t now_ms) {
  if (!started_ || channel >= kChannels || !std::isfinite(value)) return;
  const float scaled_one = value * kScale[channel];
  if (scaled_one > 32767.0f || scaled_one < -32767.0f) return;  // refused before anything moves
  observe_clock(now_ms);
  uint64_t bucket = 0;
  if (!locate(now_ms, &bucket)) return;       // stamped before reset()
  if (bucket < newest_bucket_) return;        // a late sample for a minute that is already closed
  if (bucket > newest_bucket_) advance_to(bucket);
  sum_[channel] += value;
  ++n_[channel];
  const float mean = sum_[channel] / static_cast<float>(n_[channel]);
  const int16_t stored = static_cast<int16_t>(std::lround(mean * kScale[channel]));
  int16_t& slot = data_[channel][static_cast<size_t>(newest_bucket_ % kBuckets)];
  if (slot != stored) { slot = stored; ++seq_; }
}

int16_t EnvHistory::at(size_t channel, size_t newest_offset) const {
  if (!started_ || channel >= kChannels || newest_offset >= count_) return kHistGap;
  return data_[channel][static_cast<size_t>((newest_bucket_ - newest_offset) % kBuckets)];
}

float EnvHistory::value(size_t channel, size_t newest_offset, bool* valid) const {
  const int16_t raw = at(channel, newest_offset);
  if (valid) *valid = raw != kHistGap;
  return raw == kHistGap ? 0.0f : static_cast<float>(raw) / kScale[channel];
}

EnvColumn env_column(const EnvHistory& history, size_t channel, size_t span, size_t columns, size_t col) {
  EnvColumn out;
  if (columns == 0 || col >= columns) return out;
  if (span > EnvHistory::kBuckets) span = EnvHistory::kBuckets;
  // Column `col` covers buckets [lo, hi) counted back from the newest; the newest column starts at offset 0.
  const size_t from_right = columns - 1 - col;
  const size_t lo = from_right * span / columns;
  size_t hi = (from_right + 1) * span / columns;
  if (hi <= lo) hi = lo + 1;
  float sum = 0.0f;
  for (size_t o = lo; o < hi; ++o) {
    bool ok = false;
    const float v = history.value(channel, o, &ok);
    if (!ok) return out;  // a missing minute inside the run: the column is a gap, so no line is drawn across it
    sum += v;
  }
  out.valid = true;
  out.value = sum / static_cast<float>(hi - lo);
  return out;
}

void env_columns(const EnvHistory& history, size_t channel, size_t span, size_t columns, EnvColumn* out) {
  if (!out) return;
  for (size_t col = 0; col < columns; ++col) out[col] = env_column(history, channel, span, columns, col);
}

}  // namespace orcsdr::dead_air

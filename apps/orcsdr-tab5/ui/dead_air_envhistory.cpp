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
  start_ms_ = now_ms;
  newest_bucket_ = 0;
  count_ = 1;
  started_ = true;
  ++seq_;
}

void EnvHistory::advance_to(uint32_t bucket) {
  const uint32_t steps = bucket - newest_bucket_;
  const uint32_t fill = steps < kBuckets ? steps : static_cast<uint32_t>(kBuckets);
  for (uint32_t k = 1; k <= fill; ++k) {  // minutes with no sample are gaps, never carried-over values
    const size_t slot = (bucket - fill + k) % kBuckets;
    for (size_t c = 0; c < kChannels; ++c) data_[c][slot] = kHistGap;
  }
  for (size_t c = 0; c < kChannels; ++c) { sum_[c] = 0.0f; n_[c] = 0; }
  count_ = count_ + steps > kBuckets ? kBuckets : count_ + steps;
  newest_bucket_ = bucket;
}

void EnvHistory::add(size_t channel, float value, uint32_t now_ms) {
  if (!started_ || channel >= kChannels || !std::isfinite(value)) return;
  const uint32_t bucket = (now_ms - start_ms_) / kBucketMs;
  if (bucket < newest_bucket_) return;  // a late sample for a minute that is already closed
  if (bucket > newest_bucket_) advance_to(bucket);
  const float scaled_one = value * kScale[channel];
  if (scaled_one > 32767.0f || scaled_one < -32767.0f) return;
  sum_[channel] += value;
  ++n_[channel];
  const float mean = sum_[channel] / static_cast<float>(n_[channel]);
  const int16_t stored = static_cast<int16_t>(std::lround(mean * kScale[channel]));
  int16_t& slot = data_[channel][newest_bucket_ % kBuckets];
  if (slot != stored) { slot = stored; ++seq_; }
}

int16_t EnvHistory::at(size_t channel, size_t newest_offset) const {
  if (!started_ || channel >= kChannels || newest_offset >= count_) return kHistGap;
  return data_[channel][(newest_bucket_ - newest_offset) % kBuckets];
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
  size_t n = 0;
  for (size_t o = lo; o < hi; ++o) {
    bool ok = false;
    const float v = history.value(channel, o, &ok);
    if (ok) { sum += v; ++n; }
  }
  if (n) { out.valid = true; out.value = sum / static_cast<float>(n); }
  return out;
}

void env_columns(const EnvHistory& history, size_t channel, size_t span, size_t columns, EnvColumn* out) {
  if (!out) return;
  for (size_t col = 0; col < columns; ++col) out[col] = env_column(history, channel, span, columns, col);
}

}  // namespace orcsdr::dead_air

#pragma once
// Temperature, humidity and pressure history for the Environment page charts: one averaged value per minute for the last
// 24 hours. Pure logic (no I2C, no clock, no drawing). Every stored value is the mean of accepted sensor samples inside
// that minute; a minute with no valid sample stays a gap (kHistGap) and a chart never draws a line across it.
#include "dead_air_model.hpp"
#include <cstddef>
#include <cstdint>

namespace orcsdr::dead_air {

class EnvHistory {
 public:
  static constexpr size_t kChannels = 3;               // temperature, humidity, pressure (EnvChannel order)
  static constexpr size_t kBuckets = 1440;             // 24 h of one-minute buckets
  static constexpr uint32_t kBucketMs = 60000;
  // Stored scaled so a bucket is an int16: C and %RH in hundredths, hPa in tenths.
  static constexpr float kScale[kChannels] = {100.0f, 100.0f, 10.0f};

  void reset(uint32_t now_ms);
  // `value` is an accepted sample in C, %RH or hPa; `now_ms` is when it was taken. Values outside the int16 range are
  // refused (never wrapped). Samples that arrive for an earlier minute than the newest are ignored.
  void add(size_t channel, float value, uint32_t now_ms);

  size_t count() const { return count_; }            // buckets since reset, newest included, up to kBuckets
  uint32_t seq() const { return seq_; }              // changes whenever a stored value changes
  // 0 is the newest bucket (the minute in progress). kHistGap if that minute had no valid sample or is out of range.
  int16_t at(size_t channel, size_t newest_offset) const;
  float value(size_t channel, size_t newest_offset, bool* valid) const;  // in C, %RH or hPa

 private:
  void advance_to(uint32_t bucket);
  int16_t data_[kChannels][kBuckets]{};
  float sum_[kChannels]{};
  uint32_t n_[kChannels]{};
  uint32_t start_ms_ = 0;
  uint32_t newest_bucket_ = 0;  // bucket number (minutes since start_ms_) of the newest slot
  size_t count_ = 0;
  uint32_t seq_ = 0;
  bool started_ = false;
};

// A run of buckets summarised for one chart column: the mean of the valid buckets, or invalid when all were gaps.
struct EnvColumn {
  bool valid = false;
  float value = 0.0f;
};
// One column of that split, so a chart can walk its columns without holding them all.
EnvColumn env_column(const EnvHistory& history, size_t channel, size_t span, size_t columns, size_t col);
// Splits the newest `span` buckets into `columns` columns (column columns-1 holds the newest) and fills `out`.
void env_columns(const EnvHistory& history, size_t channel, size_t span, size_t columns, EnvColumn* out);

}  // namespace orcsdr::dead_air

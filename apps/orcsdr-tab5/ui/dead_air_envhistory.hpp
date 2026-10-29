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
  // `value` is an accepted sample in C, %RH or hPa; `now_ms` is when it was taken (the 32-bit millisecond clock; elapsed
  // time is tracked in 64 bits, so a session may run past the clock's 49.7-day wrap). A value outside the int16 range is
  // refused without moving the window. A sample for an earlier minute than the newest, or from before reset(), is ignored.
  void add(size_t channel, float value, uint32_t now_ms);
  // Moves the window to the current minute even when no sample arrived, so a sensor outage shows as gaps instead of the last
  // value standing in the newest column. Call it regularly (every UI tick is fine; it does nothing within a minute).
  void tick(uint32_t now_ms);

  size_t count() const { return count_; }            // buckets since reset, newest included, up to kBuckets
  uint32_t seq() const { return seq_; }              // changes whenever a stored value changes
  // 0 is the newest bucket (the minute in progress). kHistGap if that minute had no valid sample or is out of range.
  int16_t at(size_t channel, size_t newest_offset) const;
  float value(size_t channel, size_t newest_offset, bool* valid) const;  // in C, %RH or hPa

 private:
  bool locate(uint32_t now_ms, uint64_t* bucket) const;  // bucket of a timestamp; false if before reset()
  void advance_to(uint64_t bucket);
  void observe_clock(uint32_t now_ms);
  int16_t data_[kChannels][kBuckets]{};
  float sum_[kChannels]{};
  uint32_t n_[kChannels]{};
  uint64_t elapsed_ms_ = 0;     // milliseconds since reset() at the newest clock reading seen
  uint32_t last_clock_ms_ = 0;  // that clock reading
  uint64_t newest_bucket_ = 0;  // bucket number (minutes since reset()) of the newest slot
  size_t count_ = 0;
  uint32_t seq_ = 0;
  bool started_ = false;
};

// A run of buckets summarised for one chart column: the mean of the buckets, or invalid when ANY bucket in the run is a gap
// (a column that hides a missing minute would draw a line across it).
struct EnvColumn {
  bool valid = false;
  float value = 0.0f;
};
// One column of that split, so a chart can walk its columns without holding them all.
EnvColumn env_column(const EnvHistory& history, size_t channel, size_t span, size_t columns, size_t col);
// Splits the newest `span` buckets into `columns` columns (column columns-1 holds the newest) and fills `out`.
void env_columns(const EnvHistory& history, size_t channel, size_t span, size_t columns, EnvColumn* out);

}  // namespace orcsdr::dead_air

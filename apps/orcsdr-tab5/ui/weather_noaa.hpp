#pragma once
#include <cstddef>
#include <cstdint>

namespace orcsdr::weather::noaa {
constexpr size_t kChannelCount = 7;
uint32_t channel_hz(size_t index);
int channel_index(uint32_t frequency_hz);
uint32_t nearest_channel_hz(uint32_t frequency_hz);
uint32_t next_channel_hz(uint32_t frequency_hz);
uint32_t previous_channel_hz(uint32_t frequency_hz);

class ScanPlan {
 public:
  void start();
  void cancel();
  bool active() const;
  bool complete() const;
  uint32_t current_frequency_hz() const;
  void offer(float level_dbfs);
  void advance();
  size_t sample_count() const;
  int strongest_index() const;
  uint32_t strongest_frequency_hz() const;
 private:
  size_t index_ = 0;
  size_t samples_ = 0;
  int strongest_ = -1;
  float strongest_level_ = -1000.0f;
  bool active_ = false;
  bool complete_ = false;
};
}  // namespace orcsdr::weather::noaa

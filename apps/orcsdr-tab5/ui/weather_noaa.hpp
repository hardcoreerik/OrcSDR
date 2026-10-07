#pragma once

#include "weather_model.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::weather {

struct NoaaScanSample {
  uint32_t frequency_hz = 0;
  float relative_dbfs = 0.0f;
  uint32_t sampled_uptime_ms = 0;
  bool valid = false;
};

struct NoaaScanResult {
  NoaaScanSample samples[kNoaaWeatherChannelCount]{};
  size_t sample_count = 0;
  size_t strongest_index = kInvalidChannel;
  uint32_t strongest_frequency_hz = 0;
  float strongest_dbfs = 0.0f;
  uint32_t started_uptime_ms = 0;
  uint32_t completed_uptime_ms = 0;
  bool complete = false;
};

class ScanAccumulator {
 public:
  void begin(uint32_t now_ms);
  bool active() const;
  uint32_t next_frequency_hz() const;
  bool record(size_t index, uint32_t frequency_hz, float relative_dbfs,
              uint32_t now_ms);
  const NoaaScanResult& result() const { return result_; }

 private:
  NoaaScanResult result_{};
};

}  // namespace orcsdr::weather

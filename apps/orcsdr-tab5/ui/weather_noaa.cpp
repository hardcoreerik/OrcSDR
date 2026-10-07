#include "weather_noaa.hpp"

namespace orcsdr::weather {

void ScanAccumulator::begin(uint32_t now_ms) {
  result_ = {};
  result_.started_uptime_ms = now_ms;
}

bool ScanAccumulator::active() const {
  return !result_.complete && result_.sample_count < kNoaaWeatherChannelCount;
}

uint32_t ScanAccumulator::next_frequency_hz() const {
  return active() ? noaa_channel_hz(result_.sample_count) : 0;
}

bool ScanAccumulator::record(size_t index, uint32_t frequency_hz,
                             float relative_dbfs, uint32_t now_ms) {
  if (!active() || index != result_.sample_count ||
      frequency_hz != noaa_channel_hz(index))
    return false;
  auto& sample = result_.samples[index];
  sample.frequency_hz = frequency_hz;
  sample.relative_dbfs = relative_dbfs;
  sample.sampled_uptime_ms = now_ms;
  sample.valid = true;
  if (result_.strongest_index == kInvalidChannel ||
      relative_dbfs > result_.strongest_dbfs) {
    result_.strongest_index = index;
    result_.strongest_frequency_hz = frequency_hz;
    result_.strongest_dbfs = relative_dbfs;
  }
  ++result_.sample_count;
  if (result_.sample_count == kNoaaWeatherChannelCount) {
    result_.complete = true;
    result_.completed_uptime_ms = now_ms;
  }
  return true;
}

}  // namespace orcsdr::weather

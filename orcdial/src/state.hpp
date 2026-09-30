#pragma once
#include <cstdint>
#include "dashboard.hpp"

namespace orc {
#ifndef ORCDIAL_DEFAULT_STEP_HZ
#define ORCDIAL_DEFAULT_STEP_HZ 5000
#endif
struct RadioState {
  uint32_t frequency_hz = 146520000;
  uint32_t step_hz = ORCDIAL_DEFAULT_STEP_HZ;
  int16_t gain_tenth_db = 0;
  int16_t squelch = 0;
  int16_t signal_dbm = 0;
  uint8_t mode = 1; // 1=NFM, 2=AM, 3=WFM
  uint8_t volume = 50;
  bool signal_valid = false;
  Dashboard dashboard = Dashboard::home;
};
enum class Focus : uint8_t { vfo, step, gain, squelch, volume };
constexpr uint32_t steps[] = {1, 10, 100, 1000, 2500, 5000, 10000, 12500, 25000, 100000, 1000000};
constexpr int step_count = sizeof steps / sizeof steps[0];
inline int step_index(uint32_t value) {
  for (int i = 0; i < step_count; ++i) if (steps[i] == value) return i;
  return 5;
}
inline uint32_t clamp_frequency(int64_t hz) {
  return hz < 24000 ? 24000 : hz > 1766000000 ? 1766000000 : uint32_t(hz);
}
} // namespace orc

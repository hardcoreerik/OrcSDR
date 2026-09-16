#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::am_finder {

constexpr uint32_t kSampleRateSps = 2400000;
constexpr uint32_t kCenterHz = 1122000;
constexpr uint32_t kMinHz = 530000;
constexpr uint32_t kMaxHz = 1710000;
constexpr size_t kMaxChannels = 160;

enum class Poll : uint8_t { collecting, ready, failed };

struct Report {
  uint32_t frames = 0;
  uint32_t input_drops = 0;
  float clipping_percent = 0;
};

bool start(uint32_t spacing_hz);
bool active();
void offer_iq(const uint8_t* iq, size_t bytes, uint32_t sample_rate_sps);
Poll poll();
uint8_t progress_percent();
bool finish(float* levels, size_t capacity, size_t* count, Report* report = nullptr);
void cancel();
bool self_check();

}  // namespace orcsdr::am_finder

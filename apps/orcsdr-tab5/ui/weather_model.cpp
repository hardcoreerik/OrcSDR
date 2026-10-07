#include "weather_model.hpp"

#include <cstdio>

namespace orcsdr::weather {
namespace {
constexpr uint32_t kChannels[kNoaaWeatherChannelCount] = {
    162400000u, 162425000u, 162450000u, 162475000u,
    162500000u, 162525000u, 162550000u};
}

uint32_t noaa_channel_hz(size_t index) {
  return index < kNoaaWeatherChannelCount ? kChannels[index] : 0u;
}

size_t noaa_channel_index(uint32_t frequency_hz) {
  for (size_t i = 0; i < kNoaaWeatherChannelCount; ++i)
    if (kChannels[i] == frequency_hz) return i;
  return kInvalidChannel;
}

uint32_t nearest_noaa_channel(uint32_t frequency_hz) {
  size_t best = 0;
  uint32_t distance = UINT32_MAX;
  for (size_t i = 0; i < kNoaaWeatherChannelCount; ++i) {
    const uint32_t d = kChannels[i] > frequency_hz ? kChannels[i] - frequency_hz
                                                    : frequency_hz - kChannels[i];
    if (d < distance) {
      distance = d;
      best = i;
    }
  }
  return kChannels[best];
}

uint32_t step_noaa_channel(uint32_t frequency_hz, int direction) {
  size_t index = noaa_channel_index(frequency_hz);
  if (index == kInvalidChannel) index = noaa_channel_index(nearest_noaa_channel(frequency_hz));
  if (direction < 0)
    index = (index + kNoaaWeatherChannelCount - 1) % kNoaaWeatherChannelCount;
  else if (direction > 0)
    index = (index + 1) % kNoaaWeatherChannelCount;
  return kChannels[index];
}

Freshness classify_freshness(bool valid, uint32_t age_seconds, FreshnessPolicy policy) {
  if (!valid) return Freshness::unavailable;
  if (age_seconds <= policy.live_seconds) return Freshness::live;
  if (age_seconds <= policy.recent_seconds) return Freshness::recent;
  if (age_seconds <= policy.stale_seconds) return Freshness::stale;
  return Freshness::expired;
}

const char* source_label(SourceKind source) {
  switch (source) {
    case SourceKind::direct_rf: return "RF";
    case SourceKind::local_sensor: return "LOCAL";
    case SourceKind::local_derived: return "ESTIMATE";
    case SourceKind::cache: return "CACHE";
    case SourceKind::online: return "ONLINE";
  }
  return "UNKNOWN";
}

void format_age(char* output, size_t capacity, uint32_t age_seconds) {
  if (!output || capacity == 0) return;
  if (age_seconds < 60)
    std::snprintf(output, capacity, "%us ago", static_cast<unsigned>(age_seconds));
  else if (age_seconds < 3600)
    std::snprintf(output, capacity, "%um ago", static_cast<unsigned>(age_seconds / 60));
  else
    std::snprintf(output, capacity, "%uh ago", static_cast<unsigned>(age_seconds / 3600));
}

}  // namespace orcsdr::weather

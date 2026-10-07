#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::weather {

enum class SourceKind : uint8_t { direct_rf, local_sensor, local_derived, cache, online };
enum class Freshness : uint8_t { live, recent, stale, expired, unavailable };
enum class ValueKind : uint8_t {
  temperature_c,
  humidity_percent,
  pressure_hpa,
  wind_speed_mps,
  wind_gust_mps,
  wind_direction_deg,
  rain_rate_mm_h,
  rain_total_mm,
};
enum class OnlinePolicy : uint8_t { disabled = 0, manual = 1, automatic = 2 };

struct FreshnessPolicy {
  uint32_t live_seconds = 30;
  uint32_t recent_seconds = 300;
  uint32_t stale_seconds = 1800;
};

struct ObservationMeta {
  SourceKind source = SourceKind::cache;
  uint32_t observed_utc = 0;
  uint32_t observed_uptime_ms = 0;
  uint32_t age_seconds = 0;
  Freshness freshness = Freshness::unavailable;
  uint8_t input_count = 0;
};

struct Observation {
  ValueKind kind = ValueKind::temperature_c;
  float value = 0.0f;
  bool valid = false;
  ObservationMeta meta{};
  char source_name[32]{};
};

constexpr size_t kNoaaWeatherChannelCount = 7;
constexpr size_t kInvalidChannel = static_cast<size_t>(-1);

uint32_t noaa_channel_hz(size_t index);
size_t noaa_channel_index(uint32_t frequency_hz);
uint32_t nearest_noaa_channel(uint32_t frequency_hz);
uint32_t step_noaa_channel(uint32_t frequency_hz, int direction);

Freshness classify_freshness(bool valid, uint32_t age_seconds, FreshnessPolicy policy);
const char* source_label(SourceKind source);
void format_age(char* output, size_t capacity, uint32_t age_seconds);
constexpr OnlinePolicy default_online_policy() { return OnlinePolicy::disabled; }
constexpr bool valid_online_policy(uint8_t value) {
  return value <= static_cast<uint8_t>(OnlinePolicy::automatic);
}

}  // namespace orcsdr::weather

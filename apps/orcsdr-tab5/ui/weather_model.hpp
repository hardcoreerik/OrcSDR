#pragma once
#include <cstddef>
#include <cstdint>

namespace orcsdr::weather {

enum class SourceKind : uint8_t { direct_rf, local_sensor, local_derived, cache, online };
enum class Freshness : uint8_t { live, recent, stale, expired, unavailable };
enum class ValueKind : uint8_t {
  temperature_c, humidity_percent, pressure_hpa, wind_speed_mps,
  wind_gust_mps, wind_direction_deg, rain_rate_mm_h, rain_total_mm
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
  char source_label[32]{};
};

struct Consensus {
  bool valid = false;
  ValueKind kind = ValueKind::temperature_c;
  float value = 0.0f;
  float spread = 0.0f;
  uint32_t oldest_age_seconds = 0;
  uint8_t input_count = 0;
  SourceKind source = SourceKind::local_derived;
};

const char* source_label(SourceKind source);
const char* source_key(SourceKind source);
const char* value_key(ValueKind kind);
Freshness classify_freshness(const Observation& observation, uint32_t age_seconds);
bool consensus(const Observation* observations, size_t count, ValueKind kind,
               uint8_t minimum_inputs, Consensus* output);

}  // namespace orcsdr::weather

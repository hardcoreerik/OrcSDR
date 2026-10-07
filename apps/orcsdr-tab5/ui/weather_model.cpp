#include "weather_model.hpp"
#include <algorithm>
#include <cfloat>

namespace orcsdr::weather {
namespace {
struct Threshold { uint32_t live, recent, stale; };
Threshold threshold(ValueKind kind) {
  switch (kind) {
    case ValueKind::rain_rate_mm_h:
    case ValueKind::wind_speed_mps:
    case ValueKind::wind_gust_mps:
    case ValueKind::wind_direction_deg: return {120, 900, 3600};
    case ValueKind::temperature_c:
    case ValueKind::humidity_percent:
    case ValueKind::pressure_hpa: return {300, 1800, 10800};
    case ValueKind::rain_total_mm: return {900, 3600, 21600};
  }
  return {300, 1800, 10800};
}
}

const char* source_label(SourceKind source) {
  switch (source) {
    case SourceKind::direct_rf: return "RF";
    case SourceKind::local_sensor: return "LOCAL";
    case SourceKind::local_derived: return "DERIVED";
    case SourceKind::cache: return "CACHE";
    case SourceKind::online: return "ONLINE";
  }
  return "UNKNOWN";
}

const char* source_key(SourceKind source) {
  switch (source) {
    case SourceKind::direct_rf: return "direct_rf";
    case SourceKind::local_sensor: return "local_sensor";
    case SourceKind::local_derived: return "local_derived";
    case SourceKind::cache: return "cache";
    case SourceKind::online: return "online";
  }
  return "unknown";
}

const char* value_key(ValueKind kind) {
  switch (kind) {
    case ValueKind::temperature_c: return "temperature_c";
    case ValueKind::humidity_percent: return "humidity_percent";
    case ValueKind::pressure_hpa: return "pressure_hpa";
    case ValueKind::wind_speed_mps: return "wind_speed_mps";
    case ValueKind::wind_gust_mps: return "wind_gust_mps";
    case ValueKind::wind_direction_deg: return "wind_direction_deg";
    case ValueKind::rain_rate_mm_h: return "rain_rate_mm_h";
    case ValueKind::rain_total_mm: return "rain_total_mm";
  }
  return "unknown";
}

Freshness classify_freshness(const Observation& observation, uint32_t age_seconds) {
  if (!observation.valid) return Freshness::unavailable;
  const auto t = threshold(observation.kind);
  if (age_seconds <= t.live) return Freshness::live;
  if (age_seconds <= t.recent) return Freshness::recent;
  if (age_seconds <= t.stale) return Freshness::stale;
  return Freshness::expired;
}

bool consensus(const Observation* observations, size_t count, ValueKind kind,
               uint8_t minimum_inputs, Consensus* output) {
  if (!output) return false;
  *output = {};
  float sum = 0.0f, lo = FLT_MAX, hi = -FLT_MAX;
  uint32_t oldest = 0;
  uint8_t used = 0;
  if (observations) {
    for (size_t i = 0; i < count; ++i) {
      const auto& o = observations[i];
      if (!o.valid || o.kind != kind || o.meta.source != SourceKind::local_sensor) continue;
      sum += o.value;
      lo = std::min(lo, o.value);
      hi = std::max(hi, o.value);
      oldest = std::max(oldest, o.meta.age_seconds);
      if (used != UINT8_MAX) ++used;
    }
  }
  if (used < minimum_inputs || used == 0) return false;
  output->valid = true;
  output->kind = kind;
  output->value = sum / static_cast<float>(used);
  output->spread = hi - lo;
  output->oldest_age_seconds = oldest;
  output->input_count = used;
  output->source = SourceKind::local_derived;
  return true;
}

}  // namespace orcsdr::weather

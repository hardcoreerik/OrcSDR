#include "dead_air_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace orcsdr::dead_air {

void format_clock(uint32_t unix_time, uint32_t monotonic_ms, char* out, size_t capacity) {
  if (!out || capacity == 0) return;
  if (unix_time != 0) {
    const uint32_t day = unix_time % 86400u;
    std::snprintf(out, capacity, "%02lu:%02lu:%02lu", static_cast<unsigned long>(day / 3600u),
                  static_cast<unsigned long>(day / 60u % 60u), static_cast<unsigned long>(day % 60u));
  } else {
    const uint32_t s = monotonic_ms / 1000u;
    std::snprintf(out, capacity, "T+%02lu:%02lu", static_cast<unsigned long>(s / 60u % 100u), static_cast<unsigned long>(s % 60u));
  }
}

void format_event_row(const Event& e, char* out, size_t capacity) {
  if (!out || capacity == 0) return;
  char clock[16];
  format_clock(e.unix_time, e.monotonic_ms, clock, sizeof(clock));
  const unsigned long seq = static_cast<unsigned long>(e.sequence);
  // A mark or sweep record made with no receiver tuned says so, instead of showing a frequency that never existed.
  char where[24];
  if (e.frequency_hz) std::snprintf(where, sizeof(where), "%.3f MHz", static_cast<double>(e.frequency_hz) / 1e6);
  else std::snprintf(where, sizeof(where), "no receiver");
  switch (e.kind) {
    case Kind::rf_rise:
    case Kind::audio_burst:
      std::snprintf(out, capacity, "#%lu %s %s %.1f dBFS +%.0f dB", seq, clock, e.kind == Kind::rf_rise ? "RF RISE" : "AUDIO",
                    static_cast<double>(e.value), static_cast<double>(e.value - e.reference));
      break;
    case Kind::temperature_change:
      std::snprintf(out, capacity, "#%lu %s TEMP %.1f C %+.1f", seq, clock, static_cast<double>(e.value),
                    static_cast<double>(e.value - e.reference));
      break;
    case Kind::humidity_change:
      std::snprintf(out, capacity, "#%lu %s HUMIDITY %.1f %% %+.1f", seq, clock, static_cast<double>(e.value),
                    static_cast<double>(e.value - e.reference));
      break;
    case Kind::pressure_change:
      std::snprintf(out, capacity, "#%lu %s PRESSURE %.1f hPa %+.2f", seq, clock, static_cast<double>(e.value),
                    static_cast<double>(e.value - e.reference));
      break;
    case Kind::manual_marker:
      std::snprintf(out, capacity, "#%lu %s MARK %s", seq, clock, where);
      break;
    case Kind::sweep_start:
      std::snprintf(out, capacity, "#%lu %s SWEEP START %s", seq, clock, where);
      break;
    case Kind::sweep_stop:
      std::snprintf(out, capacity, "#%lu %s SWEEP STOP %lu hops", seq, clock, static_cast<unsigned long>(e.sweep_hops));
      break;
    default:
      std::snprintf(out, capacity, "#%lu %s %s", seq, clock, kind_name(e.kind));
      break;
  }
}

void format_duration(uint32_t seconds, char* out, size_t capacity) {
  if (!out || capacity == 0) return;
  const unsigned long h = seconds / 3600u, m = seconds / 60u % 60u, s = seconds % 60u;
  if (h) std::snprintf(out, capacity, "%luh %02lum", h, m);
  else if (m) std::snprintf(out, capacity, "%lum %02lus", m, s);
  else std::snprintf(out, capacity, "%lus", s);
}

StripScale strip_scale(const int16_t* tenths, size_t count, bool lines, float baseline_dbfs, float limit_dbfs,
                       float min_span_db) {
  float lo = 1e9f, hi = -1e9f;
  const auto take = [&](float v) { lo = std::min(lo, v); hi = std::max(hi, v); };
  for (size_t i = 0; tenths && i < count; ++i)
    if (tenths[i] != kHistGap) take(static_cast<float>(tenths[i]) / 10.0f);
  if (lines) { take(baseline_dbfs); take(limit_dbfs); }
  if (lo > hi) return {};  // nothing measured: the strip shows its notice, so any range will do
  lo -= 3.0f;
  hi += 3.0f;
  if (hi - lo < min_span_db) {
    const float mid = (hi + lo) * 0.5f;
    lo = mid - min_span_db * 0.5f;
    hi = mid + min_span_db * 0.5f;
  }
  return {lo, hi};
}

int strip_y(float dbfs, const StripScale& scale, int top, int height) {
  if (height < 2 || scale.hi <= scale.lo) return top;
  const float norm = std::min(1.0f, std::max(0.0f, (dbfs - scale.lo) / (scale.hi - scale.lo)));
  return top + (height - 1) - static_cast<int>(std::lround(norm * static_cast<float>(height - 1)));
}

}  // namespace orcsdr::dead_air

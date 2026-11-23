#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "receiver_band_plan.hpp"

namespace orcsdr::am {

// Parse a keypad kHz string and validate against the AM broadcast band before narrowing to uint32_t.
inline bool parse_frequency_khz(const char* entry, uint32_t* out_hz) {
  if (!entry || !entry[0] || !out_hz) return false;
  for (const char* p = entry; *p; ++p) {
    if ((*p < '0' || *p > '9') && *p != '.') return false;
  }
  char* end = nullptr;
  const double khz = std::strtod(entry, &end);
  if (end == entry || *end != '\0' || !std::isfinite(khz)) return false;
  const double hz_double = khz * 1000.0;
  if (!std::isfinite(hz_double)) return false;
  const double min_hz = static_cast<double>(receiver_bands::kAmBroadcast.min_hz);
  const double max_hz = static_cast<double>(receiver_bands::kAmBroadcast.max_hz);
  if (hz_double < min_hz - 0.5 || hz_double > max_hz + 0.5) return false;
  const long long rounded = std::llround(hz_double);
  if (rounded < static_cast<long long>(receiver_bands::kAmBroadcast.min_hz) ||
      rounded > static_cast<long long>(receiver_bands::kAmBroadcast.max_hz)) {
    return false;
  }
  *out_hz = static_cast<uint32_t>(rounded);
  return true;
}

}  // namespace orcsdr::am

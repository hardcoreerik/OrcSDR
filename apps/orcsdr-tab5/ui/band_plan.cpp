#include "band_plan.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace orcsdr::band_plan {

Demod demod_for(uint32_t frequency_hz) {
  if (frequency_hz >= 87500000u && frequency_hz <= 108000000u) return Demod::wfm;
  if (frequency_hz >= 118000000u && frequency_hz <= 137000000u) return Demod::am;
  if (frequency_hz <= 30000000u) return Demod::am;
  return Demod::nfm;
}

const char* demod_name(Demod demod) {
  switch (demod) {
    case Demod::wfm: return "WFM";
    case Demod::am: return "AM";
    default: return "NFM";
  }
}

uint32_t clamp(uint32_t frequency_hz) { return std::clamp(frequency_hz, kMinHz, kMaxHz); }

uint32_t cycle_step(uint32_t current_hz, int direction) {
  size_t nearest = 0;
  uint32_t best = UINT32_MAX;
  for (size_t i = 0; i < sizeof(kStepsHz) / sizeof(kStepsHz[0]); ++i) {
    const uint32_t d = kStepsHz[i] > current_hz ? kStepsHz[i] - current_hz : current_hz - kStepsHz[i];
    if (d < best) {
      best = d;
      nearest = i;
    }
  }
  const size_t last = sizeof(kStepsHz) / sizeof(kStepsHz[0]) - 1;
  if (best != 0) return kStepsHz[nearest];   // not on the list: snap first
  if (direction > 0) return kStepsHz[std::min(nearest + 1, last)];
  return kStepsHz[nearest > 0 ? nearest - 1 : 0];
}

uint32_t step_frequency(uint32_t frequency_hz, uint32_t step_hz, int direction) {
  const uint64_t base = frequency_hz;
  if (direction < 0) return clamp(base > step_hz + kMinHz ? static_cast<uint32_t>(base - step_hz) : kMinHz);
  return clamp(static_cast<uint32_t>(std::min<uint64_t>(base + step_hz, kMaxHz)));
}

uint32_t parse_mhz(const char* text) {
  if (text == nullptr) return 0;
  uint64_t whole = 0;
  uint64_t fraction = 0;
  uint64_t scale = 1;
  bool seen_dot = false;
  bool seen_digit = false;
  for (const char* p = text; *p; ++p) {
    if (*p == '.') {
      if (seen_dot) return 0;
      seen_dot = true;
    } else if (std::isdigit(static_cast<unsigned char>(*p))) {
      seen_digit = true;
      if (!seen_dot) {
        whole = whole * 10 + static_cast<uint64_t>(*p - '0');
        if (whole > 10000000ull) return 0;
      } else if (scale < 1000000ull) {   // microhertz beyond 1 Hz are dropped
        fraction = fraction * 10 + static_cast<uint64_t>(*p - '0');
        scale *= 10;
      }
    } else if (*p != ' ') {
      return 0;
    }
  }
  if (!seen_digit) return 0;
  // Large whole numbers are kilohertz: "5000" is 5 MHz, "1090" stays 1090 MHz.
  uint64_t hz;
  if (!seen_dot && whole >= 2000) hz = whole * 1000ull;
  else hz = whole * 1000000ull + fraction * 1000000ull / scale;
  if (hz < kMinHz || hz > kMaxHz) return 0;
  return static_cast<uint32_t>(hz);
}

}  // namespace orcsdr::band_plan

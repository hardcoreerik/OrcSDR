#include "sstv_tune.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace orcsdr::sstv_tune {
namespace {
constexpr uint32_t kSteps[] = {10, 100, 500, 1000, 5000, 12500, 25000};

uint16_t rgb565(int r, int g, int b) {
  r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
  return static_cast<uint16_t>(((r & 248) << 8) | ((g & 252) << 3) | (b >> 3));
}
}  // namespace

const char* demod_name(Demod demod) {
  switch (demod) {
    case Demod::usb: return "USB";
    case Demod::lsb: return "LSB";
    default: return "NFM";
  }
}

Demod default_demod(uint32_t hz) {
  if (hz < 10000000u) return Demod::lsb;
  if (hz < 30000000u) return Demod::usb;
  return Demod::nfm;
}

uint32_t clamp_hz(uint32_t hz) { return std::clamp(hz, kMinHz, kMaxHz); }
uint32_t clamp_hz64(uint64_t hz) { return hz > kMaxHz ? kMaxHz : clamp_hz(static_cast<uint32_t>(hz)); }   // saturate, never wrap

size_t step_count() { return sizeof(kSteps) / sizeof(kSteps[0]); }
uint32_t step_hz(size_t index) { return kSteps[std::min(index, step_count() - 1)]; }
size_t step_index(uint32_t step) {
  for (size_t i = 0; i < step_count(); ++i)
    if (kSteps[i] == step) return i;
  return 3;   // 1 kHz
}
uint32_t default_step(Demod demod) { return demod == Demod::nfm ? 12500u : 100u; }

uint32_t nudge(uint32_t hz, uint32_t step, int direction) {
  if (step == 0 || direction == 0) return clamp_hz(hz);
  const uint64_t grid = hz / step;
  int64_t next;
  if (direction > 0) next = static_cast<int64_t>(grid) + 1;
  else next = (hz % step == 0) ? static_cast<int64_t>(grid) - 1 : static_cast<int64_t>(grid);
  if (next < 0) next = 0;
  return clamp_hz64(static_cast<uint64_t>(next) * step);
}

uint32_t from_touch(uint32_t center_hz, uint32_t span_hz, int x, int width, uint32_t step) {
  if (width <= 0 || span_hz == 0) return clamp_hz(center_hz);
  const int clamped = std::clamp(x, 0, width - 1);
  const int64_t offset = (static_cast<int64_t>(clamped) * span_hz) / width - static_cast<int64_t>(span_hz) / 2;
  int64_t f = static_cast<int64_t>(center_hz) + offset;
  if (f < 0) f = 0;
  if (step > 1) f = ((f + static_cast<int64_t>(step) / 2) / step) * step;
  return clamp_hz64(static_cast<uint64_t>(f));
}

int to_x(uint32_t center_hz, uint32_t span_hz, uint32_t hz, int width) {
  if (width <= 0 || span_hz == 0) return 0;
  const int64_t offset = static_cast<int64_t>(hz) - static_cast<int64_t>(center_hz) + static_cast<int64_t>(span_hz) / 2;
  const int64_t x = (offset * width) / static_cast<int64_t>(span_hz);
  return static_cast<int>(std::clamp<int64_t>(x, 0, width - 1));
}

TuneError tuning_error(Demod demod, float measured, uint32_t pulses) {
  TuneError e;
  if (demod == Demod::nfm || pulses < 4 || !(measured > 800.0f && measured < 1600.0f)) return e;
  const int32_t off = static_cast<int32_t>(std::lround(measured - kSyncToneHz));
  e.valid = true;
  e.correction_hz = demod == Demod::usb ? off : -off;
  return e;
}

uint16_t palette565(uint8_t v) {
  // dark teal -> teal -> amber -> white
  const float t = v / 255.0f;
  if (t < 0.5f) {
    const float u = t / 0.5f;
    return rgb565(static_cast<int>(2 + 10 * u), static_cast<int>(10 + 150 * u), static_cast<int>(20 + 150 * u));
  }
  if (t < 0.85f) {
    const float u = (t - 0.5f) / 0.35f;
    return rgb565(static_cast<int>(12 + 243 * u), static_cast<int>(160 + 50 * u), static_cast<int>(170 - 150 * u));
  }
  const float u = (t - 0.85f) / 0.15f;
  return rgb565(255, static_cast<int>(210 + 45 * u), static_cast<int>(20 + 235 * u));
}

uint16_t tint565(Tint tint) {
  switch (tint) {
    case Tint::header: return rgb565(40, 200, 230);
    case Tint::receiving: return rgb565(60, 230, 90);
    case Tint::recovering: return rgb565(235, 80, 60);
    default: return rgb565(60, 100, 190);
  }
}

bool parse_entry(const char* text, uint32_t* frequency_hz) {
  if (text == nullptr || frequency_hz == nullptr || text[0] == '\0') return false;
  double value = 0, scale = 0;
  size_t digits = 0, whole = 0;
  bool dot = false;
  for (const char* c = text; *c; ++c) {
    if (*c == '.') {
      if (dot) return false;
      dot = true;
      scale = 1;
    } else if (*c >= '0' && *c <= '9') {
      if (++digits > 12) return false;
      if (dot) { scale /= 10; value += (*c - '0') * scale; }
      else { value = value * 10 + (*c - '0'); ++whole; }
    } else {
      return false;
    }
  }
  if (digits == 0) return false;
  const bool khz = !dot && whole >= 5;
  const double hz = value * (khz ? 1e3 : 1e6);
  if (hz < kMinHz || hz > kMaxHz) return false;
  *frequency_hz = static_cast<uint32_t>(hz + 0.5);
  return true;
}

void format_mhz(char* out, size_t size, uint32_t frequency_hz) {
  std::snprintf(out, size, "%lu.%05lu", static_cast<unsigned long>(frequency_hz / 1000000u),
                static_cast<unsigned long>((frequency_hz % 1000000u) / 10u));
}

bool self_check() {
  uint32_t parsed = 0;
  return parse_entry("145.5", &parsed) && parsed == 145500000 && default_demod(7171000) == Demod::lsb && default_demod(14230000) == Demod::usb &&
         default_demod(145500000) == Demod::nfm && nudge(14230000, 1000, 1) == 14231000 &&
         nudge(14230500, 1000, 1) == 14231000 && nudge(14230500, 1000, -1) == 14230000 &&
         from_touch(145500000, 240000, 120, 240, 1000) == 145500000 &&
         tuning_error(Demod::usb, 1250.0f, 8).correction_hz == 50 && tuning_error(Demod::lsb, 1250.0f, 8).correction_hz == -50;
}

}  // namespace orcsdr::sstv_tune

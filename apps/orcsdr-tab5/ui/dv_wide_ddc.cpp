#include "dv_wide_ddc.hpp"

#include <cmath>

namespace orcsdr::dv {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kCicRate = 5;
// Input is 2*u8-255 (+-255), table Q15, CIC gain 5^3 = 125: full-scale input maps to +-1.0 after this scale.
constexpr float kScale = 1.0f / (255.0f * 32767.0f * 125.0f);
}  // namespace

WideDdc::WideDdc() noexcept {
  for (size_t m = 0; m < kTable; ++m) {
    const double a = 2.0 * kPi * static_cast<double>(m) / static_cast<double>(kTable);
    table_[2 * m] = static_cast<int16_t>(std::lround(std::cos(a) * 32767.0));
    table_[2 * m + 1] = static_cast<int16_t>(std::lround(std::sin(a) * 32767.0));
  }
  // 17-tap Hamming low-pass at 480 kS/s, cutoff 120 kHz (passband +-70 kHz, stop from 170 kHz), unity DC gain.
  const double fc = 120000.0 / (kInputRate / kCicRate);
  const int mid = static_cast<int>(kFirTaps / 2);
  double sum = 0.0;
  std::array<double, kFirTaps> t{};
  for (size_t n = 0; n < kFirTaps; ++n) {
    const int k = static_cast<int>(n) - mid;
    const double sinc = k == 0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * k) / (kPi * k);
    const double w = 0.54 - 0.46 * std::cos(2.0 * kPi * static_cast<double>(n) / static_cast<double>(kFirTaps - 1));
    t[n] = sinc * w;
    sum += t[n];
  }
  // The CIC droops the channel position (+60 kHz) by a known amount; fold its inverse into the taps so the channel
  // arrives at unity gain: |sin(pi f / fs_out) / (R sin(pi f / fs_in))|^3 with R = 5.
  const double f = static_cast<double>(kChannelOffsetHz);
  const double droop = std::pow(std::sin(kPi * f / (kInputRate / kCicRate)) /
                                (kCicRate * std::sin(kPi * f / kInputRate)), 3.0);
  for (size_t n = 0; n < kFirTaps; ++n) taps_[n] = static_cast<float>(t[n] / sum / droop);
  set_offset_hz(0);
  reset();
}

void WideDdc::reset() noexcept {
  hist_re_.fill(0.0f);
  hist_im_.fill(0.0f);
  pos_ = 0;
  half_ = 0;
  i1r_ = i2r_ = i3r_ = i1i_ = i2i_ = i3i_ = 0;
  d1r_ = d2r_ = d3r_ = d1i_ = d2i_ = d3i_ = 0;
  cic_phase_ = 0;
  index_ = 0;
}

void WideDdc::set_offset_hz(int32_t offset_hz) noexcept {
  // Multiply by exp(-j 2 pi (offset - 60k) n / fs): moves the wanted frequency to +60 kHz.
  const int32_t shift_hz = offset_hz - kChannelOffsetHz;
  const int32_t m = static_cast<int32_t>(std::lround(static_cast<double>(shift_hz) / kGridHz));
  const int32_t n = static_cast<int32_t>(kTable);
  step_ = static_cast<uint32_t>(((m % n) + n) % n);
  offset_hz_ = m * kGridHz + kChannelOffsetHz;
}

size_t WideDdc::process_cu8(const uint8_t* cu8, size_t pairs, Iq240* out, size_t capacity) noexcept {
  if (capacity < pairs / kDecimation + 1) {
    dropped_ += pairs;
    return 0;
  }
  size_t produced = 0;
  uint32_t i1r = i1r_, i2r = i2r_, i3r = i3r_, i1i = i1i_, i2i = i2i_, i3i = i3i_;
  uint32_t idx = index_;
  const uint32_t step = step_;
  const int16_t* const tab = table_.data();
  uint8_t cphase = cic_phase_;
  for (size_t n = 0; n < pairs; ++n) {
    const int32_t xi = 2 * static_cast<int32_t>(cu8[2 * n]) - 255;
    const int32_t xq = 2 * static_cast<int32_t>(cu8[2 * n + 1]) - 255;
    const int32_t c = tab[2 * idx], s = tab[2 * idx + 1];
    idx += step;
    if (idx >= kTable) idx -= static_cast<uint32_t>(kTable);
    // (xi + j xq) * (c - j s)
    const uint32_t re = static_cast<uint32_t>(xi * c + xq * s);
    const uint32_t im = static_cast<uint32_t>(xq * c - xi * s);
    i1r += re; i2r += i1r; i3r += i2r;
    i1i += im; i2i += i1i; i3i += i2i;
    if (++cphase < kCicRate) continue;
    cphase = 0;
    // combs at 480 kS/s
    const uint32_t c1r = i3r - d1r_; d1r_ = i3r;
    const uint32_t c2r = c1r - d2r_; d2r_ = c1r;
    const uint32_t c3r = c2r - d3r_; d3r_ = c2r;
    const uint32_t c1i = i3i - d1i_; d1i_ = i3i;
    const uint32_t c2i = c1i - d2i_; d2i_ = c1i;
    const uint32_t c3i = c2i - d3i_; d3i_ = c2i;
    hist_re_[pos_] = static_cast<float>(static_cast<int32_t>(c3r)) * kScale;
    hist_im_[pos_] = static_cast<float>(static_cast<int32_t>(c3i)) * kScale;
    if (++pos_ == kFirTaps) pos_ = 0;
    if (++half_ < 2) continue;
    half_ = 0;
    float ar = 0.0f, ai = 0.0f;
    size_t k = pos_;   // oldest sample
    for (size_t t = 0; t < kFirTaps; ++t) {
      ar += taps_[kFirTaps - 1 - t] * hist_re_[k];
      ai += taps_[kFirTaps - 1 - t] * hist_im_[k];
      if (++k == kFirTaps) k = 0;
    }
    out[produced++] = Iq240{ar, ai};
  }
  i1r_ = i1r; i2r_ = i2r; i3r_ = i3r; i1i_ = i1i; i2i_ = i2i; i3i_ = i3i;
  index_ = idx;
  cic_phase_ = cphase;
  return produced;
}

}  // namespace orcsdr::dv

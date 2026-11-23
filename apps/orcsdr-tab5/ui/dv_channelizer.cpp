#include "dv_channelizer.hpp"

#include <cmath>

namespace orcsdr::dv {

Channelizer::Channelizer() noexcept {
  // Hamming-windowed sinc, cutoff 18 kHz of 240 kS/s, unity DC gain.
  const double fc = 18000.0 / Channelizer::kInputRate;
  const double centre = (kTaps - 1) / 2.0;
  double sum = 0;
  std::array<double, kTaps> t{};
  for (size_t i = 0; i < kTaps; ++i) {
    const double x = static_cast<double>(i) - centre;
    const double sinc = x == 0 ? 2.0 * fc : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
    const double window = 0.54 - 0.46 * std::cos(2.0 * M_PI * static_cast<double>(i) / (kTaps - 1));
    t[i] = sinc * window;
    sum += t[i];
  }
  for (size_t i = 0; i < kTaps; ++i) taps_[i] = static_cast<float>(t[i] / sum);
}

void Channelizer::reset() noexcept {
  hist_re_.fill(0.f);
  hist_im_.fill(0.f);
  pos_ = 0;
  phase_ = 0;
  decim_ = 0;
  power_sum_ = 0;
  power_n_ = 0;
}

size_t Channelizer::process_cu8(const uint8_t* cu8, size_t pairs, Iq48* out, size_t capacity) noexcept {
  if (cu8 == nullptr || out == nullptr) return 0;
  size_t written = 0;
  constexpr float kScale = 1.0f / 127.5f;
  for (size_t n = 0; n < pairs; ++n) {
    const float i = (static_cast<float>(cu8[2 * n]) - 127.5f) * kScale;
    const float q = (static_cast<float>(cu8[2 * n + 1]) - 127.5f) * kScale;
    float re, im;   // multiply by exp(-j*pi*phase/2): moves +60 kHz down to DC
    switch (phase_) {
      case 0: re = i; im = q; break;
      case 1: re = q; im = -i; break;
      case 2: re = -i; im = -q; break;
      default: re = -q; im = i; break;
    }
    phase_ = static_cast<uint8_t>((phase_ + 1) & 3);
    hist_re_[pos_] = re;
    hist_im_[pos_] = im;
    pos_ = pos_ + 1 == kTaps ? 0 : pos_ + 1;
    if (++decim_ < kDecimation) continue;
    decim_ = 0;
    if (written >= capacity) {
      ++dropped_;
      continue;
    }
    float acc_re = 0, acc_im = 0;
    size_t idx = pos_;   // oldest sample
    for (size_t k = 0; k < kTaps; ++k) {
      acc_re += taps_[kTaps - 1 - k] * hist_re_[idx];
      acc_im += taps_[kTaps - 1 - k] * hist_im_[idx];
      idx = idx + 1 == kTaps ? 0 : idx + 1;
    }
    out[written++] = {acc_re, acc_im};
    power_sum_ += static_cast<double>(acc_re) * acc_re + static_cast<double>(acc_im) * acc_im;
    ++power_n_;
  }
  return written;
}

float Channelizer::take_level() noexcept {
  if (power_n_ == 0) return 0.f;
  const float level = static_cast<float>(std::sqrt(power_sum_ / power_n_));
  power_sum_ = 0;
  power_n_ = 0;
  return level;
}

Decimator2::Decimator2() noexcept {
  const double fc = 9000.0 / 48000.0;
  const double centre = (kTaps - 1) / 2.0;
  double sum = 0;
  std::array<double, kTaps> t{};
  for (size_t i = 0; i < kTaps; ++i) {
    const double x = static_cast<double>(i) - centre;
    const double sinc = x == 0 ? 2.0 * fc : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
    t[i] = sinc * (0.54 - 0.46 * std::cos(2.0 * M_PI * static_cast<double>(i) / (kTaps - 1)));
    sum += t[i];
  }
  for (size_t i = 0; i < kTaps; ++i) taps_[i] = static_cast<float>(t[i] / sum);
}

void Decimator2::reset() noexcept {
  hist_re_.fill(0.f);
  hist_im_.fill(0.f);
  pos_ = 0;
  phase_ = 0;
}

size_t Decimator2::process(const Iq48* in, size_t count, Iq48* out, size_t capacity) noexcept {
  size_t written = 0;
  for (size_t n = 0; n < count; ++n) {
    hist_re_[pos_] = in[n].re;
    hist_im_[pos_] = in[n].im;
    pos_ = pos_ + 1 == kTaps ? 0 : pos_ + 1;
    phase_ ^= 1u;
    if (phase_ != 0 || written >= capacity) continue;   // keep every second sample
    float re = 0, im = 0;
    size_t idx = pos_;
    for (size_t k = 0; k < kTaps; ++k) {
      re += taps_[kTaps - 1 - k] * hist_re_[idx];
      im += taps_[kTaps - 1 - k] * hist_im_[idx];
      idx = idx + 1 == kTaps ? 0 : idx + 1;
    }
    out[written++] = {re, im};
  }
  return written;
}

}  // namespace orcsdr::dv

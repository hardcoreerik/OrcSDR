#pragma once
// Reference FFT256 from interleaved unsigned 8-bit IQ; never synthesizes RF.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace orcsdr::waveshare4b {
constexpr size_t kFftSize = 256;
struct Spectrum {
  std::array<uint8_t, kFftSize> values{};
  bool real_samples = false;
};
inline Spectrum spectrum_from_cu8(const uint8_t* data, size_t bytes) {
  Spectrum out;
  if (!data || bytes < kFftSize * 2) return out;
  struct Complex {float re, im;};
  std::array<Complex, kFftSize> a{};
  constexpr float pi = 3.14159265358979323846f;
  for (size_t i = 0; i < kFftSize; ++i) {
    const float window = 0.5f - 0.5f * cosf(2.f * pi * static_cast<float>(i) / 255.f);
    a[i] = {(static_cast<float>(data[i * 2]) - 127.5f) * window,
            (static_cast<float>(data[i * 2 + 1]) - 127.5f) * window};
  }
  for (size_t i = 1, j = 0; i < kFftSize; ++i) {
    size_t bit = kFftSize >> 1;
    while (j & bit) {j ^= bit; bit >>= 1;}
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= kFftSize; len <<= 1) {
    const float angle = -2.f * pi / static_cast<float>(len);
    const float wr = cosf(angle), wi = sinf(angle);
    for (size_t start = 0; start < kFftSize; start += len) {
      float cr = 1.f, ci = 0.f;
      for (size_t j = 0; j < len / 2; ++j) {
        Complex &p = a[start + j], &q = a[start + j + len / 2];
        const float qr = q.re * cr - q.im * ci, qi = q.re * ci + q.im * cr;
        const float pr = p.re, pi_ = p.im;
        p = {pr + qr, pi_ + qi};
        q = {pr - qr, pi_ - qi};
        const float nr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = nr;
      }
    }
  }
  // Relative intensity, NOT calibrated dBm. DC is bin 128 after shift.
  for (size_t i = 0; i < kFftSize; ++i) {
    const size_t shifted = (i + kFftSize / 2) % kFftSize;
    const float p = a[i].re * a[i].re + a[i].im * a[i].im;
    const float db = 10.f * log10f(p + 1.f);
    const int intensity = static_cast<int>((db - 12.f) * 4.0f);
    out.values[shifted] = static_cast<uint8_t>(std::max(0, std::min(255, intensity)));
  }
  out.real_samples = true;
  return out;
}
} // namespace orcsdr::waveshare4b

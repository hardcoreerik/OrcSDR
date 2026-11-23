#include "spectrum_compute.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace orcsdr::spectrum_compute {

void init_window(float* window) {
  constexpr float kPi = 3.14159265358979323846f;
  for (size_t index = 0; index < kBins; ++index) {
    window[index] = 0.5f - 0.5f * cosf(2.0f * kPi * index / (kBins - 1));
  }
}

void welch_power(const uint8_t* iq, size_t windows, const Buffers& buffers) {
  float* const real = buffers.real;
  float* const imaginary = buffers.imaginary;
  const float* const window = buffers.window;
  const size_t window_bytes = kBins * 2;
  for (size_t b = 0; b < kBins; ++b) buffers.power[b] = 0.0f;

  constexpr float kPi = 3.14159265358979323846f;
  for (size_t w = 0; w < windows; ++w) {
    const uint8_t* base = iq + w * window_bytes;
    float mean_i = 0.0f;
    float mean_q = 0.0f;
    for (size_t index = 0; index < kBins; ++index) {
      mean_i += static_cast<float>(base[index * 2]);
      mean_q += static_cast<float>(base[index * 2 + 1]);
    }
    mean_i /= kBins;
    mean_q /= kBins;
    for (size_t index = 0; index < kBins; ++index) {
      real[index] = (static_cast<float>(base[index * 2]) - mean_i) * window[index];
      imaginary[index] = (static_cast<float>(base[index * 2 + 1]) - mean_q) * window[index];
    }
    for (size_t index = 1, reversed = 0; index < kBins; ++index) {
      size_t bit = kBins >> 1;
      for (; reversed & bit; bit >>= 1) reversed ^= bit;
      reversed ^= bit;
      if (index < reversed) {
        std::swap(real[index], real[reversed]);
        std::swap(imaginary[index], imaginary[reversed]);
      }
    }
    for (size_t length = 2; length <= kBins; length <<= 1) {
      const float angle = -2.0f * kPi / length;
      const float step_real = cosf(angle);
      const float step_imaginary = sinf(angle);
      for (size_t base_i = 0; base_i < kBins; base_i += length) {
        float twiddle_real = 1;
        float twiddle_imaginary = 0;
        for (size_t offset = 0; offset < length / 2; ++offset) {
          const size_t upper = base_i + offset;
          const size_t lower = upper + length / 2;
          const float product_real = real[lower] * twiddle_real - imaginary[lower] * twiddle_imaginary;
          const float product_imaginary = real[lower] * twiddle_imaginary + imaginary[lower] * twiddle_real;
          real[lower] = real[upper] - product_real;
          imaginary[lower] = imaginary[upper] - product_imaginary;
          real[upper] += product_real;
          imaginary[upper] += product_imaginary;
          const float next_real = twiddle_real * step_real - twiddle_imaginary * step_imaginary;
          twiddle_imaginary = twiddle_real * step_imaginary + twiddle_imaginary * step_real;
          twiddle_real = next_real;
        }
      }
    }
    for (size_t bin = 0; bin < kBins; ++bin) {
      const size_t shifted = (bin + kBins / 2) % kBins;
      const float p = real[shifted] * real[shifted] + imaginary[shifted] * imaginary[shifted];
      buffers.power[bin] += p;
    }
  }
}

Levels update_levels(const Buffers& buffers, size_t windows, bool trace_valid, bool home_active,
                     size_t first_bin, size_t last_bin) {
  Levels result{-120.0f, -120.0f, kBins / 2};
  const float inv_w = 1.0f / static_cast<float>(windows);
  for (size_t bin = 0; bin < kBins; ++bin) {
    const float level = 10.0f * log10f(buffers.power[bin] * inv_w + 1.0f);
    /* EMA average (cyan) + slow peak-hold (orange) for interference spotting. */
    buffers.smooth[bin] = trace_valid ? (0.78f * buffers.smooth[bin] + 0.22f * level) : level;
    if (!trace_valid || level > buffers.peak[bin]) {
      buffers.peak[bin] = level;
    } else {
      buffers.peak[bin] = 0.995f * buffers.peak[bin] + 0.005f * level;
    }
    buffers.levels[bin] = home_active ? level : buffers.smooth[bin];
    if (bin >= first_bin && bin < last_bin) {
      result.maximum = std::max(result.maximum, std::max(buffers.levels[bin], buffers.peak[bin]));
      if (buffers.levels[bin] > result.strongest) {
        result.strongest = buffers.levels[bin];
        result.strongest_bin = bin;
      }
    }
  }
  return result;
}

}  // namespace orcsdr::spectrum_compute

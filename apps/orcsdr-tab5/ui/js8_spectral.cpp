#include "js8_spectral.hpp"

#include <cmath>

namespace orcsdr::js8::spectral {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float tone_power(const int16_t* samples, size_t count, float hz, float sample_rate) {
  const float phase_step = -2.0f * kPi * hz / sample_rate;
  const float wr = std::cos(phase_step);
  const float wi = std::sin(phase_step);
  float zr = 1.0f;
  float zi = 0.0f;
  float re = 0.0f;
  float im = 0.0f;

  for (size_t i = 0; i < count; ++i) {
    const float sample = static_cast<float>(samples[i]);
    re += sample * zr;
    im += sample * zi;

    const float next_r = zr * wr - zi * wi;
    zi = zr * wi + zi * wr;
    zr = next_r;

    if ((i & 255u) == 255u) {
      const float mag2 = zr * zr + zi * zi;
      if (mag2 > 0.0f) {
        const float inv = 1.0f / std::sqrt(mag2);
        zr *= inv;
        zi *= inv;
      }
    }
  }

  const float inv = 1.0f / static_cast<float>(count);
  re *= inv;
  im *= inv;
  return re * re + im * im;
}

}  // namespace

size_t power_grid(const int16_t* samples, size_t count, Submode submode,
                  const Config& config, const OutputGrid& output) {
  if (samples == nullptr || output.cells == nullptr || config.bin_count == 0 ||
      config.rows_per_symbol == 0 || output.row_capacity == 0 ||
      output.stride < config.bin_count || config.first_hz < 0.0f ||
      config.bin_spacing_hz <= 0.0f) {
    return 0;
  }

  const Profile& p = profile(submode);
  const size_t symbol_samples = p.symbol_samples;
  if (symbol_samples == 0 || count < symbol_samples) return 0;

  const size_t hop = symbol_samples / config.rows_per_symbol;
  if (hop == 0) return 0;

  size_t rows = 0;
  for (size_t start = 0;
       start + symbol_samples <= count && rows < output.row_capacity;
       start += hop, ++rows) {
    float* row = output.cells + rows * output.stride;
    for (size_t bin = 0; bin < config.bin_count; ++bin) {
      const float hz = config.first_hz +
                       static_cast<float>(bin) * config.bin_spacing_hz;
      if (hz >= static_cast<float>(p.sample_rate_hz) * 0.5f) return rows;
      row[bin] = tone_power(samples + start, symbol_samples, hz,
                            static_cast<float>(p.sample_rate_hz));
    }
  }
  return rows;
}

bool grid_view(float* cells, size_t rows, size_t bins, size_t stride,
               sync::EnergyGrid* out) {
  if (out == nullptr || cells == nullptr || rows == 0 || bins == 0 || stride < bins)
    return false;
  *out = sync::EnergyGrid{cells, rows, bins, stride};
  return true;
}

bool self_check() {
  Config config{};
  return config.bin_spacing_hz == 6.25f && config.rows_per_symbol == 2;
}

}  // namespace orcsdr::js8::spectral

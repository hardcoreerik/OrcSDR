#include "js8_snr.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace orcsdr::js8::snr {
namespace {

constexpr size_t kGuardNear = 8;
constexpr size_t kGuardFar = 14;
constexpr size_t kMaxNoiseSamples = 1024;
constexpr float kExpQuarterQuantile = 0.28768207245f;

}  // namespace

bool estimate(const Profile& profile, const sync::EnergyGrid& grid,
              const sync::Geometry& geometry,
              const sync::Candidate& candidate,
              const RawFrame& frame,
              const Calibration& calibration,
              float* snr_db) {
  if (snr_db == nullptr || grid.cells == nullptr ||
      grid.stride < grid.bins || geometry.rows_per_symbol == 0 ||
      geometry.bins_per_tone == 0 || profile.tone_count < 2 ||
      frame.submode != profile.submode ||
      !std::isfinite(calibration.offset_db))
    return false;

  const size_t tone_step = geometry.bins_per_tone;
  const size_t last_bin =
      static_cast<size_t>(candidate.base_bin) +
      static_cast<size_t>(profile.tone_count - 1) * tone_step;
  if (last_bin >= grid.bins) return false;

  float noise_samples[kMaxNoiseSamples];
  size_t noise_count = 0;

  const size_t low_end =
      candidate.base_bin >= kGuardNear * tone_step
          ? candidate.base_bin - kGuardNear * tone_step
          : 0;
  const size_t low_start =
      candidate.base_bin >= kGuardFar * tone_step
          ? candidate.base_bin - kGuardFar * tone_step
          : 0;
  const size_t high_start = last_bin + kGuardNear * tone_step + 1;
  const size_t high_end =
      std::min(last_bin + kGuardFar * tone_step + 1, grid.bins);

  float tx_sum = 0.0f;
  size_t symbol_count = 0;

  for (size_t symbol = 0; symbol < profile.channel_symbols; ++symbol) {
    const size_t row =
        static_cast<size_t>(candidate.start_row) +
        symbol * geometry.rows_per_symbol;
    if (row >= grid.rows) return false;

    const uint8_t tone = frame.tones[symbol];
    if (tone >= profile.tone_count) return false;
    const size_t tx_bin =
        static_cast<size_t>(candidate.base_bin) +
        static_cast<size_t>(tone) * tone_step;
    const float tx = grid.cells[row * grid.stride + tx_bin];
    if (!std::isfinite(tx) || tx < 0.0f) return false;
    tx_sum += tx;
    ++symbol_count;

    const float* cells = grid.cells + row * grid.stride;
    for (size_t bin = low_start;
         bin < low_end && noise_count < kMaxNoiseSamples; ++bin) {
      if (std::isfinite(cells[bin]) && cells[bin] >= 0.0f)
        noise_samples[noise_count++] = cells[bin];
    }
    for (size_t bin = high_start;
         bin < high_end && noise_count < kMaxNoiseSamples; ++bin) {
      if (std::isfinite(cells[bin]) && cells[bin] >= 0.0f)
        noise_samples[noise_count++] = cells[bin];
    }
  }

  if (symbol_count == 0 || noise_count < 40) return false;

  const size_t quantile_index = noise_count / 4;
  std::nth_element(noise_samples,
                   noise_samples + quantile_index,
                   noise_samples + noise_count);
  const float noise =
      noise_samples[quantile_index] / kExpQuarterQuantile;
  if (!(noise > 0.0f) || !std::isfinite(noise)) return false;

  const float tx = tx_sum / static_cast<float>(symbol_count);
  const float signal = tx - noise;
  float result = kFloorDb;
  if (signal > 0.0f) {
    const float spacing_hz =
        static_cast<float>(profile.tone_spacing_millihz) / 1000.0f;
    const float ratio =
        10.0f * std::log10(signal / noise) -
        10.0f * std::log10(2500.0f / spacing_hz);
    result = ratio + calibration.offset_db;
  }

  if (!std::isfinite(result)) return false;
  if (result < kFloorDb) result = kFloorDb;
  *snr_db = result;
  return true;
}

bool self_check() {
  return kFloorDb < 0.0f && kExpQuarterQuantile > 0.0f;
}

}  // namespace orcsdr::js8::snr

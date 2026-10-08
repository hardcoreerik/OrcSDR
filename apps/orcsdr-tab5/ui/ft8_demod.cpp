#include "ft8_demod.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orcsdr::ftx::demod {
namespace {

bool config_valid(const Config& config) {
  return std::isfinite(config.llr_gain) && config.llr_gain > 0.0f &&
         std::isfinite(config.max_abs_llr) && config.max_abs_llr > 0.0f;
}

float energy_at(const sync::EnergyGrid& grid, std::size_t row,
                std::size_t bin) {
  return grid.cells[row * grid.stride + bin];
}

}  // namespace

bool soft_demodulate(const ModeProfile& profile, const sync::EnergyGrid& grid,
                     const sync::Geometry& geometry,
                     const sync::Candidate& candidate, Result* result,
                     const Config& config) {
  if (result == nullptr || !implementation_ready(profile.mode) ||
      !profile_valid(profile) || grid.cells == nullptr || grid.rows == 0 ||
      grid.bins == 0 || grid.stride < grid.bins ||
      geometry.rows_per_symbol == 0 || geometry.bins_per_tone == 0 ||
      !config_valid(config))
    return false;

  const std::size_t final_bin =
      static_cast<std::size_t>(candidate.base_bin) +
      static_cast<std::size_t>(profile.tone_count - 1) *
          geometry.bins_per_tone;
  if (final_bin >= grid.bins) return false;

  Result out{};
  double contrast_sum = 0.0;
  std::size_t symbol_count = 0;

  for (std::size_t block_index = 0;
       block_index < profile.data_block_count; ++block_index) {
    const auto& block = profile.data[block_index];
    for (std::size_t offset = 0; offset < block.length; ++offset) {
      const std::size_t channel_symbol = block.first_symbol + offset;
      const std::size_t row =
          static_cast<std::size_t>(candidate.start_row) +
          channel_symbol * geometry.rows_per_symbol;
      if (row >= grid.rows) return false;

      float tone_energy[8]{};
      float sum = 0.0f;
      float strongest = -1.0f;
      float second = -1.0f;
      for (uint8_t tone = 0; tone < profile.tone_count; ++tone) {
        const std::size_t bin =
            static_cast<std::size_t>(candidate.base_bin) +
            static_cast<std::size_t>(tone) * geometry.bins_per_tone;
        const float value = energy_at(grid, row, bin);
        if (!std::isfinite(value) || value < 0.0f) return false;
        tone_energy[tone] = value;
        sum += value;
        if (value > strongest) {
          second = strongest;
          strongest = value;
        } else if (value > second) {
          second = value;
        }
      }

      const float mean = sum / profile.tone_count;
      constexpr float kEpsilon = 1.0e-12f;
      contrast_sum +=
          (strongest - std::max(0.0f, second)) / (mean + kEpsilon);
      ++symbol_count;

      for (uint8_t bit = 0; bit < profile.bits_per_tone; ++bit) {
        float max_zero = -std::numeric_limits<float>::infinity();
        float max_one = -std::numeric_limits<float>::infinity();
        const uint8_t shift =
            static_cast<uint8_t>(profile.bits_per_tone - 1 - bit);
        for (uint8_t tone = 0; tone < profile.tone_count; ++tone) {
          const bool one = ((profile.tone_bits[tone] >> shift) & 1u) != 0;
          if (one)
            max_one = std::max(max_one, tone_energy[tone]);
          else
            max_zero = std::max(max_zero, tone_energy[tone]);
        }
        if (out.bit_count >= out.llr.size() || !std::isfinite(max_zero) ||
            !std::isfinite(max_one))
          return false;
        float llr =
            config.llr_gain * (max_zero - max_one) / (mean + kEpsilon);
        llr = std::clamp(llr, -config.max_abs_llr, config.max_abs_llr);
        out.llr[out.bit_count++] = llr;
      }
    }
  }

  if (out.bit_count != kMaxSoftBits || symbol_count == 0) return false;
  out.mean_symbol_contrast =
      static_cast<float>(contrast_sum / symbol_count);
  *result = out;
  return true;
}

bool self_check() {
  return kMaxSoftBits == 174;
}

}  // namespace orcsdr::ftx::demod

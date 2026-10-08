#include "ft8_sync.hpp"

#include <algorithm>
#include <cmath>

namespace orcsdr::ftx::sync {
namespace {

bool grid_valid(const EnergyGrid& grid) {
  return grid.cells != nullptr && grid.rows != 0 && grid.bins != 0 &&
         grid.stride >= grid.bins;
}

bool geometry_valid(const Geometry& geometry) {
  return geometry.rows_per_symbol != 0 && geometry.bins_per_tone != 0;
}

float energy_at(const EnergyGrid& grid, std::size_t row, std::size_t bin) {
  return grid.cells[row * grid.stride + bin];
}

bool finite_nonnegative(float value) {
  return std::isfinite(value) && value >= 0.0f;
}

bool overlaps(const Candidate& a, const Candidate& b, const SearchConfig& config) {
  const int dt = std::abs(static_cast<int>(a.start_row) -
                          static_cast<int>(b.start_row));
  const int df = std::abs(static_cast<int>(a.base_bin) -
                          static_cast<int>(b.base_bin));
  return dt <= config.suppress_time_rows &&
         df <= config.suppress_frequency_bins;
}

void erase_candidate(Candidate* output, std::size_t* count, std::size_t index) {
  for (std::size_t i = index + 1; i < *count; ++i) output[i - 1] = output[i];
  --(*count);
}

void insert_sorted(Candidate* output, std::size_t* count, std::size_t capacity,
                   const Candidate& candidate) {
  if (capacity == 0) return;
  std::size_t position = 0;
  while (position < *count && output[position].score >= candidate.score) ++position;
  if (position >= capacity) return;
  const std::size_t new_count = std::min(*count + 1, capacity);
  for (std::size_t i = new_count - 1; i > position; --i) {
    output[i] = output[i - 1];
  }
  output[position] = candidate;
  *count = new_count;
}

}  // namespace

bool score_candidate(const ModeProfile& profile, const EnergyGrid& grid,
                     const Geometry& geometry, uint16_t start_row,
                     uint16_t base_bin, Candidate* result) {
  if (result == nullptr || !profile_valid(profile) || !grid_valid(grid) ||
      !geometry_valid(geometry) || profile.sync_block_count == 0 ||
      profile.tone_count < 2) {
    return false;
  }

  const std::size_t final_row =
      static_cast<std::size_t>(start_row) +
      static_cast<std::size_t>(profile.channel_symbols - 1) *
          geometry.rows_per_symbol;
  const std::size_t final_bin =
      static_cast<std::size_t>(base_bin) +
      static_cast<std::size_t>(profile.tone_count - 1) *
          geometry.bins_per_tone;
  if (final_row >= grid.rows || final_bin >= grid.bins) return false;

  double expected_sum = 0.0;
  double competing_sum = 0.0;
  std::size_t observations = 0;

  for (std::size_t block_index = 0;
       block_index < profile.sync_block_count; ++block_index) {
    const auto& block = profile.sync[block_index];
    for (std::size_t symbol = 0; symbol < block.length; ++symbol) {
      const std::size_t row =
          static_cast<std::size_t>(start_row) +
          static_cast<std::size_t>(block.first_symbol + symbol) *
              geometry.rows_per_symbol;
      const uint8_t expected_tone = block.tones[symbol];
      double other_sum = 0.0;
      for (uint8_t tone = 0; tone < profile.tone_count; ++tone) {
        const std::size_t bin =
            static_cast<std::size_t>(base_bin) +
            static_cast<std::size_t>(tone) * geometry.bins_per_tone;
        const float value = energy_at(grid, row, bin);
        if (!finite_nonnegative(value)) return false;
        if (tone == expected_tone) {
          expected_sum += value;
        } else {
          other_sum += value;
        }
      }
      competing_sum += other_sum /
                       static_cast<double>(profile.tone_count - 1);
      ++observations;
    }
  }

  if (observations == 0) return false;
  const float expected_mean =
      static_cast<float>(expected_sum / observations);
  const float competing_mean =
      static_cast<float>(competing_sum / observations);
  constexpr float kEpsilon = 1.0e-12f;
  const float denominator = expected_mean + competing_mean + kEpsilon;
  const float score =
      (expected_mean - competing_mean) / denominator;
  if (!std::isfinite(score)) return false;

  *result =
      Candidate{start_row, base_bin, score, expected_mean, competing_mean};
  return true;
}

std::size_t search(const ModeProfile& profile, const EnergyGrid& grid,
                   const Geometry& geometry, const SearchConfig& config,
                   Candidate* output, std::size_t capacity) {
  if (output == nullptr || capacity == 0 ||
      !implementation_ready(profile.mode) || !profile_valid(profile) ||
      !grid_valid(grid) || !geometry_valid(geometry) ||
      !std::isfinite(config.min_score) || config.min_score < -1.0f ||
      config.min_score > 1.0f) {
    return 0;
  }

  const std::size_t frame_rows =
      static_cast<std::size_t>(profile.channel_symbols - 1) *
          geometry.rows_per_symbol +
      1;
  const std::size_t tone_bins =
      static_cast<std::size_t>(profile.tone_count - 1) *
          geometry.bins_per_tone +
      1;
  if (frame_rows > grid.rows || tone_bins > grid.bins) return 0;

  const std::size_t max_start_exclusive = grid.rows - frame_rows + 1;
  const std::size_t max_base_exclusive = grid.bins - tone_bins + 1;
  const std::size_t start_begin =
      std::min<std::size_t>(config.first_start_row, max_start_exclusive);
  const std::size_t start_end =
      config.last_start_row_exclusive == 0
          ? max_start_exclusive
          : std::min<std::size_t>(config.last_start_row_exclusive,
                                  max_start_exclusive);
  const std::size_t base_begin =
      std::min<std::size_t>(config.first_base_bin, max_base_exclusive);
  const std::size_t base_end =
      config.last_base_bin_exclusive == 0
          ? max_base_exclusive
          : std::min<std::size_t>(config.last_base_bin_exclusive,
                                  max_base_exclusive);
  if (start_begin >= start_end || base_begin >= base_end) return 0;

  std::size_t count = 0;
  for (std::size_t row = start_begin; row < start_end; ++row) {
    for (std::size_t bin = base_begin; bin < base_end; ++bin) {
      Candidate candidate{};
      if (!score_candidate(profile, grid, geometry,
                           static_cast<uint16_t>(row),
                           static_cast<uint16_t>(bin), &candidate) ||
          candidate.score < config.min_score) {
        continue;
      }

      bool discard = false;
      for (std::size_t i = 0; i < count;) {
        if (!overlaps(candidate, output[i], config)) {
          ++i;
          continue;
        }
        if (output[i].score >= candidate.score) {
          discard = true;
          break;
        }
        erase_candidate(output, &count, i);
      }
      if (!discard) insert_sorted(output, &count, capacity, candidate);
    }
  }
  return count;
}

bool self_check() {
  float cells[90 * 16]{};
  std::fill(std::begin(cells), std::end(cells), 1.0f);
  const EnergyGrid grid{cells, 90, 16, 16};
  const Geometry geometry{};
  Candidate candidate{};
  return score_candidate(profile(Mode::ft8), grid, geometry, 0, 0,
                         &candidate) &&
         candidate.score == 0.0f;
}

}  // namespace orcsdr::ftx::sync

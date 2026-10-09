#include "js8_sync.hpp"

#include <algorithm>
#include <cmath>

namespace orcsdr::js8::sync {
namespace {

bool grid_ok(const EnergyGrid& grid, const Geometry& geometry) {
  return grid.cells != nullptr && grid.rows != 0 && grid.bins != 0 && grid.stride >= grid.bins &&
         geometry.rows_per_symbol != 0 && geometry.bins_per_tone != 0;
}

bool overlap(const Candidate& a, const Candidate& b, const SearchConfig& config) {
  const int dt = std::abs(static_cast<int>(a.start_row) - static_cast<int>(b.start_row));
  const int df = std::abs(static_cast<int>(a.base_bin) - static_cast<int>(b.base_bin));
  return dt <= static_cast<int>(config.suppress_time_rows) &&
         df <= static_cast<int>(config.suppress_frequency_bins);
}

void sort_desc(Candidate* output, size_t count) {
  std::stable_sort(output, output + count,
                   [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
}

}  // namespace

bool score_candidate(const Profile& profile, const EnergyGrid& grid, const Geometry& geometry,
                     uint16_t start_row, uint16_t base_bin, Candidate* out) {
  if (out == nullptr || !grid_ok(grid, geometry) || !profile.sync_pattern_verified) return false;
  const size_t last_symbol = profile.sync.back().first_symbol + profile.sync.back().tones.size() - 1;
  const size_t last_row = static_cast<size_t>(start_row) + last_symbol * geometry.rows_per_symbol;
  const size_t last_bin = static_cast<size_t>(base_bin) + (profile.tone_count - 1u) * geometry.bins_per_tone;
  if (last_row >= grid.rows || last_bin >= grid.bins) return false;

  float sum = 0.0f;
  size_t n = 0;
  for (const SyncBlock& block : profile.sync) {
    for (size_t i = 0; i < block.tones.size(); ++i) {
      const size_t row = static_cast<size_t>(start_row) +
                         (static_cast<size_t>(block.first_symbol) + i) * geometry.rows_per_symbol;
      const size_t expected_bin = static_cast<size_t>(base_bin) + block.tones[i] * geometry.bins_per_tone;
      const float expected = grid.cells[row * grid.stride + expected_bin];
      float competitors = 0.0f;
      for (uint8_t tone = 0; tone < profile.tone_count; ++tone) {
        if (tone == block.tones[i]) continue;
        const size_t bin = static_cast<size_t>(base_bin) + tone * geometry.bins_per_tone;
        competitors += grid.cells[row * grid.stride + bin];
      }
      competitors /= static_cast<float>(profile.tone_count - 1u);
      const float denom = expected + competitors + 1.0e-20f;
      sum += (expected - competitors) / denom;
      ++n;
    }
  }
  const float score = n ? sum / static_cast<float>(n) : -1.0f;
  if (!std::isfinite(score)) return false;
  *out = Candidate{start_row, base_bin, score};
  return true;
}

size_t search(const Profile& profile, const EnergyGrid& grid, const Geometry& geometry,
              const SearchConfig& config, Candidate* output, size_t capacity) {
  if (output == nullptr || capacity == 0 || !grid_ok(grid, geometry) || !profile.sync_pattern_verified) return 0;
  const size_t frame_rows = static_cast<size_t>(profile.channel_symbols - 1u) * geometry.rows_per_symbol + 1u;
  const size_t signal_bins = static_cast<size_t>(profile.tone_count - 1u) * geometry.bins_per_tone + 1u;
  if (frame_rows > grid.rows || signal_bins > grid.bins) return 0;

  const size_t max_start_exclusive = grid.rows - frame_rows + 1u;
  const size_t max_base_exclusive = grid.bins - signal_bins + 1u;
  const size_t start_begin = std::min<size_t>(config.first_start_row, max_start_exclusive);
  const size_t start_end = config.last_start_row_exclusive == 0
                               ? max_start_exclusive
                               : std::min<size_t>(config.last_start_row_exclusive, max_start_exclusive);
  const size_t bin_begin = std::min<size_t>(config.first_base_bin, max_base_exclusive);
  const size_t bin_end = config.last_base_bin_exclusive == 0
                             ? max_base_exclusive
                             : std::min<size_t>(config.last_base_bin_exclusive, max_base_exclusive);

  size_t count = 0;
  for (size_t row = start_begin; row < start_end; ++row) {
    for (size_t bin = bin_begin; bin < bin_end; ++bin) {
      Candidate candidate{};
      if (!score_candidate(profile, grid, geometry, static_cast<uint16_t>(row),
                           static_cast<uint16_t>(bin), &candidate) || candidate.score < config.min_score)
        continue;

      bool merged = false;
      for (size_t i = 0; i < count; ++i) {
        if (!overlap(candidate, output[i], config)) continue;
        if (candidate.score > output[i].score) output[i] = candidate;
        merged = true;
        break;
      }
      if (merged) {
        sort_desc(output, count);
        continue;
      }
      if (count < capacity) {
        output[count++] = candidate;
        sort_desc(output, count);
      } else if (candidate.score > output[count - 1].score) {
        output[count - 1] = candidate;
        sort_desc(output, count);
      }
    }
  }
  return count;
}

bool self_check() {
  return physical_layer_ready(Submode::normal) && profile(Submode::normal).channel_symbols == 79;
}

}  // namespace orcsdr::js8::sync

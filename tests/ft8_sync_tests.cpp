#include "ft8_sync.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace orcsdr::ftx;
using namespace orcsdr::ftx::sync;

static void inject_sync(const ModeProfile& p, std::vector<float>& cells,
                        std::size_t rows, std::size_t bins,
                        const Geometry& geometry, std::size_t start_row,
                        std::size_t base_bin, float energy) {
  (void)rows;
  for (std::size_t b = 0; b < p.sync_block_count; ++b) {
    const auto& block = p.sync[b];
    for (std::size_t s = 0; s < block.length; ++s) {
      const std::size_t row =
          start_row + (block.first_symbol + s) * geometry.rows_per_symbol;
      const std::size_t bin =
          base_bin + block.tones[s] * geometry.bins_per_tone;
      cells[row * bins + bin] = energy;
    }
  }
}

static void test_uniform_grid_is_not_signature() {
  std::vector<float> cells(100 * 32, 1.0f);
  EnergyGrid grid{cells.data(), 100, 32, 32};
  Candidate c{};
  assert(score_candidate(profile(Mode::ft8), grid, Geometry{}, 0, 0, &c));
  assert(std::fabs(c.score) < 1e-6f);
  Candidate out[8]{};
  SearchConfig cfg{};
  cfg.min_score = 0.20f;
  assert(search(profile(Mode::ft8), grid, Geometry{}, cfg, out, 8) == 0);
}

static void test_ft8_candidate_search() {
  constexpr std::size_t rows = 100, bins = 40;
  std::vector<float> cells(rows * bins, 1.0f);
  const auto& p = profile(Mode::ft8);
  const Geometry geometry{1, 1};
  inject_sync(p, cells, rows, bins, geometry, 7, 13, 25.0f);
  EnergyGrid grid{cells.data(), rows, bins, bins};
  Candidate out[8]{};
  SearchConfig cfg{};
  cfg.min_score = 0.50f;
  cfg.suppress_time_rows = 1;
  cfg.suppress_frequency_bins = 1;
  const std::size_t count = search(p, grid, geometry, cfg, out, 8);
  assert(count >= 1);
  assert(out[0].start_row == 7);
  assert(out[0].base_bin == 13);
  assert(out[0].score > 0.90f);
  assert(out[0].expected_mean > 20.0f);
  assert(out[0].competing_mean == 1.0f);
}

static void test_ft4_oversampled_geometry() {
  constexpr std::size_t rows = 240, bins = 64;
  std::vector<float> cells(rows * bins, 0.5f);
  const auto& p = profile(Mode::ft4);
  const Geometry geometry{2, 2};
  inject_sync(p, cells, rows, bins, geometry, 5, 11, 18.0f);
  EnergyGrid grid{cells.data(), rows, bins, bins};
  Candidate out[4]{};
  SearchConfig cfg{};
  cfg.first_start_row = 1;
  cfg.last_start_row_exclusive = 15;
  cfg.first_base_bin = 5;
  cfg.last_base_bin_exclusive = 20;
  cfg.min_score = 0.60f;
  const std::size_t count = search(p, grid, geometry, cfg, out, 4);
  assert(count >= 1);
  assert(out[0].start_row == 5);
  assert(out[0].base_bin == 11);
  assert(out[0].score > 0.90f);
}

static void test_research_pending_js8_not_searched() {
  std::vector<float> cells(100 * 32, 1.0f);
  EnergyGrid grid{cells.data(), 100, 32, 32};
  Candidate out[2]{};
  SearchConfig cfg{};
  cfg.min_score = -1.0f;
  assert(search(profile(Mode::js8_normal), grid, Geometry{}, cfg, out, 2) ==
         0);
}

static void test_invalid_energy_rejected() {
  std::vector<float> cells(100 * 32, 1.0f);
  EnergyGrid grid{cells.data(), 100, 32, 32};
  Candidate c{};
  const auto& p = profile(Mode::ft8);
  const auto& block = p.sync[0];
  const std::size_t row = block.first_symbol;
  const std::size_t expected_bin = block.tones[0];
  cells[row * 32 + expected_bin] =
      std::numeric_limits<float>::quiet_NaN();
  assert(!score_candidate(p, grid, Geometry{}, 0, 0, &c));
}

int main() {
  assert(orcsdr::ftx::sync::self_check());
  test_uniform_grid_is_not_signature();
  test_ft8_candidate_search();
  test_ft4_oversampled_geometry();
  test_research_pending_js8_not_searched();
  test_invalid_energy_rejected();
  return 0;
}

#include "js8_sync.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
  using namespace orcsdr::js8;
  using namespace orcsdr::js8::sync;
  assert(orcsdr::js8::sync::self_check());
  const Profile& p = profile(Submode::normal);
  constexpr size_t rows = 165;
  constexpr size_t bins = 24;
  constexpr uint16_t start = 3;
  constexpr uint16_t base = 6;
  std::vector<float> cells(rows * bins, 1.0f);
  const Geometry geometry{2,1};

  for (const SyncBlock& block : p.sync)
    for (size_t i = 0; i < block.tones.size(); ++i) {
      const size_t row = start + (block.first_symbol + i) * geometry.rows_per_symbol;
      const size_t bin = base + block.tones[i];
      cells[row * bins + bin] = 30.0f;
    }

  EnergyGrid grid{cells.data(), rows, bins, bins};
  Candidate scored{};
  assert(score_candidate(p, grid, geometry, start, base, &scored));
  assert(scored.score > 0.85f);

  SearchConfig config{};
  config.min_score = 0.5f;
  std::array<Candidate, 8> found{};
  const size_t n = search(p, grid, geometry, config, found.data(), found.size());
  assert(n >= 1);
  assert(found[0].start_row == start);
  assert(found[0].base_bin == base);
  assert(found[0].score > 0.85f);

  std::fill(cells.begin(), cells.end(), 1.0f);
  assert(search(p, grid, geometry, config, found.data(), found.size()) == 0);
  assert(!score_candidate(profile(Submode::fast), grid, geometry, 0, 0, &scored));

  std::puts("JS8 sync search tests: PASS");
  return 0;
}

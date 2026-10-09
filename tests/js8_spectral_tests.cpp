#include "js8_spectral.hpp"
#include "js8_sync.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr std::array<uint8_t, 79> kApiFrame{{
    4,2,5,6,1,3,0, 1,0,2,6,6,3,1,6,6,4,0,1,7,0,7,2,6,2,6,0,4,3,4,5,2,3,5,2,0,
    4,2,5,6,1,3,0, 3,4,2,5,4,5,7,0,1,6,3,6,7,0,2,3,5,6,4,5,7,4,0,0,1,7,3,6,4,
    4,2,5,6,1,3,0}};

uint32_t g_rng = 0x9e3779b9u;

float noise() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return static_cast<float>(static_cast<int32_t>(g_rng & 0xffffu) - 32768) /
         32768.0f;
}

}  // namespace

int main() {
  using namespace orcsdr::js8;
  using namespace orcsdr::js8::spectral;

  assert(orcsdr::js8::spectral::self_check());
  const Profile& p = profile(Submode::normal);

  constexpr size_t kHop = 960;
  constexpr size_t kStart = 2 * kHop;
  constexpr float kBaseHz = 900.0f;

  const size_t frame_samples =
      static_cast<size_t>(p.channel_symbols) * p.symbol_samples;
  std::vector<int16_t> pcm(kStart + frame_samples + kHop, 0);

  float phase = 0.0f;
  for (size_t symbol = 0; symbol < kApiFrame.size(); ++symbol) {
    const float hz = kBaseHz + kApiFrame[symbol] * 6.25f;
    const float step = 2.0f * kPi * hz / 12000.0f;
    for (size_t n = 0; n < p.symbol_samples; ++n) {
      const float value = 10000.0f * std::sin(phase) + 350.0f * noise();
      pcm[kStart + symbol * p.symbol_samples + n] =
          static_cast<int16_t>(value);
      phase += step;
      if (phase > 2.0f * kPi) phase -= 2.0f * kPi;
    }
  }

  Config config{};
  config.first_hz = 850.0f;
  config.bin_spacing_hz = 6.25f;
  config.bin_count = 32;
  config.rows_per_symbol = 2;

  const size_t rows =
      1 + (pcm.size() - p.symbol_samples) /
              (p.symbol_samples / config.rows_per_symbol);
  std::vector<float> grid(rows * config.bin_count);

  size_t written = power_grid(
      pcm.data(), pcm.size(), Submode::normal, config,
      OutputGrid{grid.data(), rows, config.bin_count});
  assert(written == rows);

  orcsdr::js8::sync::EnergyGrid view{};
  assert(grid_view(grid.data(), written, config.bin_count, config.bin_count,
                   &view));

  orcsdr::js8::sync::SearchConfig search{};
  search.min_score = 0.75f;
  std::array<orcsdr::js8::sync::Candidate, 8> candidates{};
  const size_t found = orcsdr::js8::sync::search(
      p, view, orcsdr::js8::sync::Geometry{2, 1}, search,
      candidates.data(), candidates.size());

  assert(found >= 1);
  assert(candidates[0].start_row == 2);
  assert(candidates[0].base_bin == 8);
  assert(candidates[0].score > 0.90f);

  // Quiet control: no signal means no sync candidate above the threshold.
  std::fill(pcm.begin(), pcm.end(), 0);
  std::fill(grid.begin(), grid.end(), 0.0f);
  written = power_grid(
      pcm.data(), pcm.size(), Submode::normal, config,
      OutputGrid{grid.data(), rows, config.bin_count});
  assert(written == rows);
  assert(grid_view(grid.data(), written, config.bin_count, config.bin_count,
                   &view));
  assert(orcsdr::js8::sync::search(
             p, view, orcsdr::js8::sync::Geometry{2, 1}, search,
             candidates.data(), candidates.size()) == 0);

  std::puts("JS8 spectral front-end tests: PASS");
  return 0;
}

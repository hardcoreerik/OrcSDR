#include "js8_frontend.hpp"
#include "js8_spectral.hpp"

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

}  // namespace

int main() {
  using namespace orcsdr::js8;
  assert(frontend::self_check());

  const Profile& p = profile(Submode::normal);
  constexpr size_t kStart = 1920;
  constexpr float kBaseHz = 900.0f;
  const size_t frame_samples =
      static_cast<size_t>(p.channel_symbols) * p.symbol_samples;
  std::vector<int16_t> pcm(kStart + frame_samples + 960, 0);

  float phase = 0.0f;
  for (size_t s = 0; s < kApiFrame.size(); ++s) {
    const float hz = kBaseHz + kApiFrame[s] * 6.25f;
    const float step = 2.0f * kPi * hz / 12000.0f;
    for (size_t n = 0; n < p.symbol_samples; ++n) {
      pcm[kStart + s * p.symbol_samples + n] =
          static_cast<int16_t>(10000.0f * std::sin(phase));
      phase += step;
      if (phase > 2.0f * kPi) phase -= 2.0f * kPi;
    }
  }

  spectral::Config spectral_config{};
  spectral_config.first_hz = 850.0f;
  spectral_config.bin_spacing_hz = 6.25f;
  spectral_config.bin_count = 32;
  spectral_config.rows_per_symbol = 2;

  const size_t rows =
      1 + (pcm.size() - p.symbol_samples) / (p.symbol_samples / 2);
  std::vector<float> energy(rows * spectral_config.bin_count, 0.0f);
  assert(spectral::power_grid(
             pcm.data(), pcm.size(), Submode::normal, spectral_config,
             spectral::OutputGrid{
                 energy.data(), rows, spectral_config.bin_count}) == rows);

  sync::EnergyGrid grid{};
  assert(spectral::grid_view(
      energy.data(), rows, spectral_config.bin_count,
      spectral_config.bin_count, &grid));

  frontend::Config config{};
  config.first_hz = 850.0f;
  config.bin_spacing_hz = 6.25f;
  config.search.min_score = 0.75f;

  std::array<frontend::RawCandidate, 4> output{};
  frontend::Stats stats{};
  const size_t count = frontend::extract(
      pcm.data(), pcm.size(), Submode::normal, grid, config,
      output.data(), output.size(), &stats);

  assert(count == 1);
  assert(stats.candidates_found == 1);
  assert(stats.candidates_demodulated == 1);
  assert(stats.frames_emitted == 1);
  assert(output[0].start_sample == kStart);
  assert(std::fabs(output[0].base_hz - kBaseHz) < 0.001f);
  assert(output[0].frame.tones == kApiFrame);
  assert(output[0].demod.sync_hits == 21);

  std::fill(energy.begin(), energy.end(), 0.0f);
  stats = frontend::Stats{};
  assert(frontend::extract(
             pcm.data(), pcm.size(), Submode::normal, grid, config,
             output.data(), output.size(), &stats) == 0);
  assert(stats.frames_emitted == 0);

  std::puts("JS8 front-end pipeline tests: PASS");
  return 0;
}

#include "ft8_codec.hpp"
#include "ft8_demod.hpp"
#include "ft8_ldpc.hpp"
#include "ft8_ldpc_decode.hpp"
#include "ft8_spectral.hpp"
#include "ft8_sync.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using orcsdr::ftx::Mode;
using orcsdr::ftx::profile;

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

std::vector<int16_t> synth_tone(double frequency_hz, std::size_t samples,
                                uint32_t sample_rate_hz, double amplitude,
                                double* phase) {
  std::vector<int16_t> out(samples);
  const double step = kTwoPi * frequency_hz / sample_rate_hz;
  double p = phase ? *phase : 0.0;
  for (std::size_t i = 0; i < samples; ++i) {
    out[i] = static_cast<int16_t>(std::lround(amplitude * std::sin(p)));
    p += step;
    if (p >= kTwoPi) p -= kTwoPi;
  }
  if (phase) *phase = p;
  return out;
}

void append(std::vector<int16_t>* dst, const std::vector<int16_t>& src) {
  dst->insert(dst->end(), src.begin(), src.end());
}

void test_streaming_single_tone_peak() {
  const auto& p = profile(Mode::ft8);
  constexpr std::size_t bins = 16;
  std::array<float, bins> grid{};
  orcsdr::ftx::spectral::Config config{};
  config.first_bin_millihz = 975000;
  config.bin_spacing_millihz = 6250;
  config.bin_count = bins;
  config.rows_per_symbol = 1;
  orcsdr::ftx::spectral::ReferenceAccumulator state{};
  assert(orcsdr::ftx::spectral::begin(
      &state, p, config,
      orcsdr::ftx::spectral::OutputGrid{grid.data(), 1, bins}));

  double phase = 0.0;
  auto pcm = synth_tone(1018.75, p.symbol_samples, p.sample_rate_hz,
                        12000.0, &phase);
  assert(orcsdr::ftx::spectral::offer(&state, pcm.data(), 17));
  assert(orcsdr::ftx::spectral::offer(&state, pcm.data() + 17, 333));
  assert(orcsdr::ftx::spectral::offer(
      &state, pcm.data() + 350, pcm.size() - 350));
  assert(orcsdr::ftx::spectral::rows_written(state) == 1);

  const auto peak =
      static_cast<std::size_t>(std::max_element(grid.begin(), grid.end()) -
                               grid.begin());
  assert(peak == 7);
  assert(grid[peak] > grid[6] * 1000.0f);
  assert(grid[peak] > grid[8] * 1000.0f);
}

void test_synthetic_ft8_pcm_to_crc_valid_codeword() {
  const auto& p = profile(Mode::ft8);

  orcsdr::ft8::codec::PayloadBits payload{};
  uint32_t prng = 0x31415926u;
  for (auto& bit : payload) {
    prng = prng * 1664525u + 1013904223u;
    bit = static_cast<uint8_t>((prng >> 31) & 1u);
  }
  const auto message = orcsdr::ft8::codec::append_crc(payload);
  const auto codeword = orcsdr::ft8::ldpc::encode(message);
  assert(orcsdr::ft8::ldpc::valid(codeword));

  orcsdr::ft8::codec::DataTones data_tones{};
  assert(orcsdr::ft8::codec::codeword_to_data_tones(codeword, &data_tones));
  const auto channel = orcsdr::ft8::codec::frame_data_tones(data_tones);

  constexpr double base_hz = 1000.0;
  constexpr std::size_t leading_rows = 2;
  std::vector<int16_t> pcm(leading_rows * p.symbol_samples, 0);
  double phase = 0.0;
  for (uint8_t tone : channel) {
    append(&pcm, synth_tone(base_hz + tone * 6.25, p.symbol_samples,
                            p.sample_rate_hz, 12000.0, &phase));
  }

  constexpr std::size_t bins = 24;
  constexpr std::size_t base_bin = 8;
  constexpr std::size_t rows = leading_rows + 79;
  std::vector<float> energies(rows * bins, 0.0f);

  orcsdr::ftx::spectral::Config spectral_config{};
  spectral_config.first_bin_millihz = 950000;
  spectral_config.bin_spacing_millihz = 6250;
  spectral_config.bin_count = bins;
  spectral_config.rows_per_symbol = 1;

  orcsdr::ftx::spectral::ReferenceAccumulator spectral{};
  assert(orcsdr::ftx::spectral::begin(
      &spectral, p, spectral_config,
      orcsdr::ftx::spectral::OutputGrid{energies.data(), rows, bins}));

  std::size_t offset = 0;
  const std::array<std::size_t, 5> chunks{{137, 509, 41, 997, 223}};
  std::size_t chunk_index = 0;
  while (offset < pcm.size()) {
    const std::size_t take =
        std::min(chunks[chunk_index++ % chunks.size()], pcm.size() - offset);
    assert(orcsdr::ftx::spectral::offer(&spectral, pcm.data() + offset, take));
    offset += take;
  }
  assert(orcsdr::ftx::spectral::rows_written(spectral) == rows);

  orcsdr::ftx::sync::EnergyGrid grid{
      energies.data(), rows, bins, bins};
  orcsdr::ftx::sync::Candidate candidates[4]{};
  orcsdr::ftx::sync::SearchConfig search{};
  search.first_start_row = 0;
  search.last_start_row_exclusive = 3;
  search.first_base_bin = 4;
  search.last_base_bin_exclusive = 12;
  search.min_score = 0.70f;
  const std::size_t candidate_count =
      orcsdr::ftx::sync::search(p, grid, orcsdr::ftx::sync::Geometry{},
                                search, candidates, 4);
  assert(candidate_count >= 1);
  assert(candidates[0].start_row == leading_rows);
  assert(candidates[0].base_bin == base_bin);
  assert(candidates[0].score > 0.90f);

  orcsdr::ftx::demod::Result demod{};
  assert(orcsdr::ftx::demod::soft_demodulate(
      p, grid, orcsdr::ftx::sync::Geometry{}, candidates[0], &demod));
  assert(demod.bit_count == 174);

  orcsdr::ft8::ldpc_decode::LlrVector llr{};
  std::copy(demod.llr.begin(), demod.llr.end(), llr.begin());
  orcsdr::ft8::ldpc_decode::Workspace workspace{};
  orcsdr::ft8::ldpc_decode::Result decoded{};
  assert(orcsdr::ft8::ldpc_decode::decode(llr, &workspace, &decoded));
  assert(decoded.converged);
  assert(orcsdr::ft8::codec::crc_valid(decoded.message));
  assert(decoded.message == message);
  assert(decoded.codeword == codeword);
}

}  // namespace

int main() {
  assert(orcsdr::ftx::spectral::self_check());
  test_streaming_single_tone_peak();
  test_synthetic_ft8_pcm_to_crc_valid_codeword();
  return 0;
}

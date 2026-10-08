#include "ft8_mode.hpp"
#include "ft8_pipeline.hpp"
#include "ft8_spectral.hpp"
#include "ft8_wav_common.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using ft8_wav_tools::Wav;
using ft8_wav_tools::parse_u8;
using ft8_wav_tools::read_wav;

int main(int argc, char** argv) {
  if (argc < 2 || argc > 5) {
    std::fprintf(stderr,
                 "usage: %s <12k-mono-i16.wav> [rows_per_symbol=2] "
                 "[bins_per_tone=1] [mode=ft8|ft4]\n",
                 argv[0]);
    return 2;
  }

  uint8_t rows_per_symbol = 2;
  uint8_t bins_per_tone = 1;
  if (argc >= 3 && !parse_u8(argv[2], &rows_per_symbol)) return 2;
  if (argc >= 4 && !parse_u8(argv[3], &bins_per_tone)) return 2;

  orcsdr::ftx::Mode mode = orcsdr::ftx::Mode::ft8;
  if (argc >= 5) {
    if (std::strcmp(argv[4], "ft8") == 0)
      mode = orcsdr::ftx::Mode::ft8;
    else if (std::strcmp(argv[4], "ft4") == 0)
      mode = orcsdr::ftx::Mode::ft4;
    else {
      std::fprintf(stderr, "mode must be ft8 or ft4\n");
      return 2;
    }
  }
  if ((rows_per_symbol != 1 && rows_per_symbol != 2 &&
       rows_per_symbol != 4) ||
      (bins_per_tone != 1 && bins_per_tone != 2)) {
    std::fprintf(stderr,
                 "rows_per_symbol must be 1, 2, or 4; "
                 "bins_per_tone must be 1 or 2\n");
    return 2;
  }

  Wav wav{};
  if (!read_wav(argv[1], &wav)) {
    std::fprintf(stderr,
                 "unsupported WAV: require RIFF PCM, mono, 12000 Hz, 16-bit\n");
    return 2;
  }

  const auto& p = orcsdr::ftx::profile(mode);
  const std::size_t hop =
      p.symbol_samples / static_cast<std::size_t>(rows_per_symbol);
  if (wav.samples.size() < p.symbol_samples) {
    std::fprintf(stderr, "WAV is shorter than one %s symbol\n", p.name);
    return 2;
  }
  const std::size_t rows =
      1 + (wav.samples.size() - p.symbol_samples) / hop;

  constexpr uint32_t kFirstMillihz = 200000;
  constexpr uint32_t kLastMillihz = 3000000;
  const uint32_t spacing =
      p.tone_spacing_millihz / bins_per_tone;
  const std::size_t bins =
      1 + (kLastMillihz - kFirstMillihz) / spacing;
  if (bins > std::numeric_limits<uint16_t>::max()) return 2;

  std::vector<float> energy(rows * bins, 0.0f);
  orcsdr::ftx::spectral::Config spectral_config{};
  spectral_config.first_bin_millihz = kFirstMillihz;
  spectral_config.bin_spacing_millihz = spacing;
  spectral_config.bin_count = static_cast<uint16_t>(bins);
  spectral_config.rows_per_symbol = rows_per_symbol;

  orcsdr::ftx::spectral::ReferenceAccumulator spectral{};
  if (!orcsdr::ftx::spectral::begin(
          &spectral, p, spectral_config,
          orcsdr::ftx::spectral::OutputGrid{
              energy.data(), rows, bins}) ||
      !orcsdr::ftx::spectral::offer(
          &spectral, wav.samples.data(), wav.samples.size())) {
    std::fprintf(stderr, "spectral analysis failed\n");
    return 1;
  }

  orcsdr::ftx::sync::EnergyGrid grid{
      energy.data(), orcsdr::ftx::spectral::rows_written(spectral),
      bins, bins};
  orcsdr::ftx::sync::Geometry geometry{
      rows_per_symbol, bins_per_tone};

  orcsdr::ftx::pipeline::Config config{};
  config.candidate_limit = 16;
  config.search.min_score = 0.10f;
  config.search.suppress_time_rows = rows_per_symbol;
  config.search.suppress_frequency_bins = bins_per_tone;

  orcsdr::ftx::pipeline::Workspace workspace{};
  std::array<orcsdr::ftx::pipeline::FrameResult, 16> results{};
  const std::size_t count = orcsdr::ftx::pipeline::decode_grid(
      p, grid, geometry, config, &workspace, results.data(), results.size());

  std::printf(
      "FTX_WAV_BENCH mode=%s file=%s samples=%zu rows=%zu bins=%zu "
      "rows_per_symbol=%u bins_per_tone=%u decoded=%zu\n",
      p.name, argv[1], wav.samples.size(), grid.rows, grid.bins,
      rows_per_symbol, bins_per_tone, count);

  for (std::size_t i = 0; i < count; ++i) {
    const auto& r = results[i];
    const double audio_hz =
        orcsdr::ftx::spectral::bin_frequency_hz(
            spectral_config, r.candidate.base_bin);
    const double start_s =
        static_cast<double>(r.candidate.start_row * hop) /
        static_cast<double>(p.sample_rate_hz);
    std::printf(
        "DECODE index=%zu start_s=%.3f audio_hz=%.3f "
        "sync=%.4f contrast=%.4f ldpc_iter=%u text=%s\n",
        i, start_s, audio_hz, r.candidate.score,
        r.mean_symbol_contrast,
        static_cast<unsigned>(r.ldpc_iterations),
        r.standard.text);
  }

  return 0;
}

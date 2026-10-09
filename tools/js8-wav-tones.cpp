#include "ft8_wav_common.hpp"
#include "js8_demod.hpp"
#include "js8_spectral.hpp"
#include "js8_sync.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {

bool parse_float(const char* text, float* out) {
  if (text == nullptr || out == nullptr || *text == '\0') return false;
  char* end = nullptr;
  const float value = std::strtof(text, &end);
  if (end == text || *end != '\0' || !std::isfinite(value)) return false;
  *out = value;
  return true;
}

void print_tones(const orcsdr::js8::RawFrame& frame) {
  for (uint8_t tone : frame.tones)
    std::putchar(static_cast<int>('0' + tone));
  std::putchar('\n');
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 5) {
    std::fprintf(
        stderr,
        "usage: %s <12k-mono-i16.wav> [first_hz=200] [last_hz=3000] "
        "[min_sync=0.45]\n",
        argv[0]);
    return 2;
  }

  float first_hz = 200.0f;
  float last_hz = 3000.0f;
  float min_sync = 0.45f;
  if (argc >= 3 && !parse_float(argv[2], &first_hz)) return 2;
  if (argc >= 4 && !parse_float(argv[3], &last_hz)) return 2;
  if (argc >= 5 && !parse_float(argv[4], &min_sync)) return 2;
  if (first_hz < 0.0f || last_hz <= first_hz || last_hz >= 6000.0f ||
      min_sync < -1.0f || min_sync > 1.0f)
    return 2;

  ft8_wav_tools::Wav wav{};
  if (!ft8_wav_tools::read_wav(argv[1], &wav)) {
    std::fprintf(stderr,
                 "unsupported WAV: require RIFF PCM, mono, 12000 Hz, 16-bit\n");
    return 2;
  }

  const auto& p = orcsdr::js8::profile(orcsdr::js8::Submode::normal);
  const float spacing =
      static_cast<float>(p.tone_spacing_millihz) / 1000.0f;
  const size_t bins =
      1 + static_cast<size_t>(std::floor((last_hz - first_hz) / spacing));
  if (bins > std::numeric_limits<uint16_t>::max()) return 2;

  const size_t hop = p.symbol_samples / 2u;
  if (wav.samples.size() < p.symbol_samples) return 2;
  const size_t rows =
      1 + (wav.samples.size() - p.symbol_samples) / hop;

  std::vector<float> energy(rows * bins, 0.0f);
  orcsdr::js8::spectral::Config spectral_config{};
  spectral_config.first_hz = first_hz;
  spectral_config.bin_spacing_hz = spacing;
  spectral_config.bin_count = static_cast<uint16_t>(bins);
  spectral_config.rows_per_symbol = 2;

  const size_t written = orcsdr::js8::spectral::power_grid(
      wav.samples.data(), wav.samples.size(), orcsdr::js8::Submode::normal,
      spectral_config,
      orcsdr::js8::spectral::OutputGrid{energy.data(), rows, bins});
  if (written == 0) {
    std::fprintf(stderr, "spectral analysis failed\n");
    return 1;
  }

  orcsdr::js8::sync::EnergyGrid grid{};
  if (!orcsdr::js8::spectral::grid_view(
          energy.data(), written, bins, bins, &grid))
    return 1;

  orcsdr::js8::sync::SearchConfig search{};
  search.min_score = min_sync;
  search.suppress_time_rows = 2;
  search.suppress_frequency_bins = 1;

  std::array<orcsdr::js8::sync::Candidate, 64> candidates{};
  const size_t found = orcsdr::js8::sync::search(
      p, grid, orcsdr::js8::sync::Geometry{2, 1}, search,
      candidates.data(), candidates.size());

  std::printf(
      "# JS8_WAV_TONES file=%s samples=%zu rows=%zu bins=%zu "
      "candidates=%zu first_hz=%.3f last_hz=%.3f\n",
      argv[1], wav.samples.size(), written, bins, found,
      static_cast<double>(first_hz), static_cast<double>(last_hz));

  size_t emitted = 0;
  for (size_t i = 0; i < found; ++i) {
    const auto& candidate = candidates[i];
    orcsdr::js8::DemodConfig demod{};
    demod.start_sample = static_cast<size_t>(candidate.start_row) * hop;
    demod.base_hz =
        first_hz + static_cast<float>(candidate.base_bin) * spacing;
    demod.min_sync_score = min_sync;

    orcsdr::js8::RawFrame frame{};
    orcsdr::js8::DemodStats stats{};
    if (!orcsdr::js8::demodulate_tones(
            wav.samples.data(), wav.samples.size(),
            orcsdr::js8::Submode::normal, demod, &frame, &stats))
      continue;

    std::printf(
        "# FRAME index=%zu start_s=%.3f base_hz=%.3f "
        "grid_sync=%.4f tone_sync=%.4f hits=%u margin=%.4f\n",
        emitted,
        static_cast<double>(demod.start_sample) / 12000.0,
        static_cast<double>(demod.base_hz),
        static_cast<double>(candidate.score),
        static_cast<double>(stats.sync_score),
        static_cast<unsigned>(stats.sync_hits),
        static_cast<double>(stats.mean_margin));
    print_tones(frame);
    ++emitted;
  }

  std::printf("# JS8_WAV_TONES_EMITTED=%zu\n", emitted);
  return 0;
}

#include "js8_frontend.hpp"

#include <algorithm>
#include <array>

namespace orcsdr::js8::frontend {

size_t extract(const int16_t* samples, size_t sample_count, Submode submode,
               const sync::EnergyGrid& grid, const Config& config,
               RawCandidate* output, size_t capacity, Stats* stats) {
  if (samples == nullptr || output == nullptr || stats == nullptr ||
      capacity == 0 || config.candidate_limit == 0 ||
      config.first_hz < 0.0f || config.bin_spacing_hz <= 0.0f ||
      config.geometry.rows_per_symbol == 0 ||
      config.geometry.bins_per_tone == 0) {
    return 0;
  }

  *stats = Stats{};
  const Profile& p = profile(submode);
  if (!physical_layer_ready(submode)) return 0;

  constexpr size_t kMaxCandidates = 64;
  std::array<sync::Candidate, kMaxCandidates> candidates{};
  const size_t requested =
      std::min<size_t>(config.candidate_limit, candidates.size());
  const size_t found = sync::search(
      p, grid, config.geometry, config.search,
      candidates.data(), requested);
  stats->candidates_found = static_cast<uint16_t>(found);

  const size_t hop = p.symbol_samples / config.geometry.rows_per_symbol;
  size_t emitted = 0;
  for (size_t i = 0; i < found && emitted < capacity; ++i) {
    const sync::Candidate& candidate = candidates[i];

    DemodConfig demod_config{};
    demod_config.start_sample =
        static_cast<size_t>(candidate.start_row) * hop;
    demod_config.base_hz =
        config.first_hz +
        static_cast<float>(candidate.base_bin) * config.bin_spacing_hz;
    demod_config.min_sync_score = config.search.min_score;

    RawFrame frame{};
    DemodStats demod_stats{};
    ++stats->candidates_demodulated;
    if (!demodulate_tones(samples, sample_count, submode, demod_config,
                          &frame, &demod_stats)) {
      continue;
    }

    RawCandidate result{};
    result.spectral = candidate;
    result.frame = frame;
    result.demod = demod_stats;
    result.start_sample = static_cast<uint32_t>(demod_config.start_sample);
    result.base_hz = demod_config.base_hz;
    output[emitted++] = result;
  }

  stats->frames_emitted = static_cast<uint16_t>(emitted);
  return emitted;
}

bool self_check() {
  Config config{};
  return config.candidate_limit == 16 &&
         config.geometry.rows_per_symbol == 2 &&
         config.geometry.bins_per_tone == 1;
}

}  // namespace orcsdr::js8::frontend

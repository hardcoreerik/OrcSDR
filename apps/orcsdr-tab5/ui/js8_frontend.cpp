#include "js8_frontend.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace orcsdr::js8::frontend {

bool refine_candidate(const int16_t* samples, size_t sample_count, Submode submode, const Config& config, size_t* start_sample, float* base_hz) {
  if (samples == nullptr || start_sample == nullptr || base_hz == nullptr) return false;
  SyncProbe best{};
  size_t best_start = *start_sample;
  float best_hz = *base_hz;
  if (!probe_sync(samples, sample_count, submode, best_start, best_hz, &best)) return false;
  if (best.hits >= 21) return false;   // a full sync match needs no refinement
  for (int dt = -2; dt <= 2; ++dt) {
    for (int df = -2; df <= 2; ++df) {
      if (dt == 0 && df == 0) continue;
      const int64_t start = static_cast<int64_t>(*start_sample) + static_cast<int64_t>(dt) * config.refine_step_samples;
      const float hz = *base_hz + static_cast<float>(df) * config.refine_step_hz;
      if (start < 0 || hz < 0.0f) continue;
      SyncProbe probe{};
      if (!probe_sync(samples, sample_count, submode, static_cast<size_t>(start), hz, &probe)) continue;
      if (probe.hits > best.hits || (probe.hits == best.hits && probe.score > best.score)) {
        best = probe;
        best_start = static_cast<size_t>(start);
        best_hz = hz;
      }
    }
  }
  const bool moved = best_start != *start_sample || best_hz != *base_hz;
  *start_sample = best_start;
  *base_hz = best_hz;
  return moved;
}

void mark_aliases(const Profile& profile, const AliasItem* items, size_t count, bool* drop) {
  if (items == nullptr || drop == nullptr) return;
  const size_t period = static_cast<size_t>(profile.symbol_samples) * 36u;   // distance between Normal sync blocks
  const size_t tolerance = static_cast<size_t>(profile.symbol_samples);
  for (size_t i = 0; i < count; ++i) drop[i] = false;
  for (size_t i = 0; i < count; ++i) {
    for (size_t j = i + 1; j < count; ++j) {
      if (std::fabs(items[i].base_hz - items[j].base_hz) > 3.2f) continue;
      const size_t a = items[i].start_sample, b = items[j].start_sample;
      const size_t gap = a > b ? a - b : b - a;
      bool alias = false;
      for (size_t k = 1; k <= 2; ++k) {
        const size_t target = period * k;
        if (gap + tolerance >= target && gap <= target + tolerance) alias = true;
      }
      if (!alias) continue;
      // keep the stronger frame: more of the signal is present in it (margin), then more sync hits
      const bool i_better = items[i].margin > items[j].margin || (items[i].margin == items[j].margin && items[i].hits >= items[j].hits);
      drop[i_better ? j : i] = true;
    }
  }
}

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

    if (config.refine && refine_candidate(samples, sample_count, submode, config, &demod_config.start_sample, &demod_config.base_hz)) ++stats->refined;

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

  if (config.resolve_aliases && emitted > 1) {
    std::array<AliasItem, kMaxCandidates> items{};
    std::array<bool, kMaxCandidates> drop{};
    for (size_t i = 0; i < emitted; ++i)
      items[i] = AliasItem{output[i].base_hz, output[i].start_sample, output[i].demod.mean_margin, output[i].demod.sync_hits};
    mark_aliases(p, items.data(), emitted, drop.data());
    size_t kept = 0;
    for (size_t i = 0; i < emitted; ++i) {
      if (drop[i]) {
        ++stats->aliases_removed;
        continue;
      }
      if (kept != i) output[kept] = output[i];
      ++kept;
    }
    emitted = kept;
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

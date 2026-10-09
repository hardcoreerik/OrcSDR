#pragma once

#include "js8_demod.hpp"
#include "js8_sync.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::frontend {

struct Config {
  sync::SearchConfig search{};
  uint16_t candidate_limit = 16;
  float first_hz = 200.0f;
  float bin_spacing_hz = 6.25f;
  sync::Geometry geometry{2, 1};
  // Refinement of each sync candidate before full demodulation: a coarse grid point is only accurate to a few Hz and a few tens of ms, and a
  // signal that falls between 6.25 Hz bins (real stations are at arbitrary frequencies) loses most of its energy there. The best of a small
  // set of frequency/time offsets, judged by the sync symbols alone, is used.
  bool refine = true;
  float refine_step_hz = 1.5625f;       // frequency offsets: -2..+2 steps (+-3.125 Hz)
  uint16_t refine_step_samples = 240;   // time offsets: -2..+2 steps (+-20 ms at 12 kS/s)
  // The three Normal sync blocks are identical, so an alignment one block (36 symbols) early or late scores two blocks out of three. Of
  // two candidates at the same frequency whose start times differ by whole sync periods, the one with the stronger frame is kept.
  bool resolve_aliases = true;
};

struct RawCandidate {
  sync::Candidate spectral{};
  RawFrame frame{};
  DemodStats demod{};
  uint32_t start_sample = 0;
  float base_hz = 0.0f;
};

struct Stats {
  uint16_t refined = 0;
  uint16_t aliases_removed = 0;
  uint16_t candidates_found = 0;
  uint16_t candidates_demodulated = 0;
  uint16_t frames_emitted = 0;
};

// One candidate as seen by the alias resolver: position, frequency and how strong the demodulated frame is.
struct AliasItem {
  float base_hz = 0.0f;
  size_t start_sample = 0;
  float margin = 0.0f;
  uint8_t hits = 0;
};

// Refines (start_sample, base_hz) in place by the best sync-only probe over the small offset grid in Config; returns true if it moved.
bool refine_candidate(const int16_t* samples, size_t sample_count, Submode submode, const Config& config, size_t* start_sample, float* base_hz);

// Marks as dropped every item that is a weaker alias (same frequency, start differing by whole sync periods) of another item.
void mark_aliases(const Profile& profile, const AliasItem* items, size_t count, bool* drop);

size_t extract(const int16_t* samples, size_t sample_count, Submode submode,
               const sync::EnergyGrid& grid, const Config& config,
               RawCandidate* output, size_t capacity, Stats* stats);

bool self_check();

}  // namespace orcsdr::js8::frontend

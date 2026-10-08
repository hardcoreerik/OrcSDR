#pragma once

#include "ft8_mode.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::ftx::sync {

struct EnergyGrid {
  const float* cells = nullptr;
  std::size_t rows = 0;
  std::size_t bins = 0;
  std::size_t stride = 0;
};

struct Geometry {
  uint8_t rows_per_symbol = 1;
  uint8_t bins_per_tone = 1;
};

struct Candidate {
  uint16_t start_row = 0;
  uint16_t base_bin = 0;
  float score = 0.0f;
  float expected_mean = 0.0f;
  float competing_mean = 0.0f;
};

struct SearchConfig {
  uint16_t first_start_row = 0;
  uint16_t last_start_row_exclusive = 0;
  uint16_t first_base_bin = 0;
  uint16_t last_base_bin_exclusive = 0;
  float min_score = 0.20f;
  uint8_t suppress_time_rows = 1;
  uint8_t suppress_frequency_bins = 1;
};

// Scores only the protocol-defined synchronization symbols. The grid contains
// linear non-negative tone energies. score is a bounded contrast metric:
// (expected - competing) / (expected + competing + epsilon).
// It is NOT SNR and must never be presented as dB.
bool score_candidate(const ModeProfile& profile, const EnergyGrid& grid,
                     const Geometry& geometry, uint16_t start_row,
                     uint16_t base_bin, Candidate* result);

// Finds strongest non-overlapping synchronization candidates without heap
// allocation. Results are sorted descending by score. Only implementation-ready
// profiles are searched; research-pending/experimental profiles return zero.
std::size_t search(const ModeProfile& profile, const EnergyGrid& grid,
                   const Geometry& geometry, const SearchConfig& config,
                   Candidate* output, std::size_t capacity);

bool self_check();

}  // namespace orcsdr::ftx::sync

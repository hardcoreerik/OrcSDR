#pragma once

#include "js8_mode.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::sync {

struct EnergyGrid {
  const float* cells = nullptr;
  size_t rows = 0;
  size_t bins = 0;
  size_t stride = 0;
};

struct Geometry {
  uint8_t rows_per_symbol = 2;
  uint8_t bins_per_tone = 1;
};

struct Candidate {
  uint16_t start_row = 0;
  uint16_t base_bin = 0;
  float score = 0.0f;
};

struct SearchConfig {
  uint16_t first_start_row = 0;
  uint16_t last_start_row_exclusive = 0;
  uint16_t first_base_bin = 0;
  uint16_t last_base_bin_exclusive = 0;
  float min_score = 0.10f;
  uint16_t suppress_time_rows = 2;
  uint16_t suppress_frequency_bins = 1;
};

bool score_candidate(const Profile& profile, const EnergyGrid& grid, const Geometry& geometry,
                     uint16_t start_row, uint16_t base_bin, Candidate* out);

size_t search(const Profile& profile, const EnergyGrid& grid, const Geometry& geometry,
              const SearchConfig& config, Candidate* output, size_t capacity);

bool self_check();

}  // namespace orcsdr::js8::sync

#pragma once

#include "js8_mode.hpp"
#include "js8_sync.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::spectral {

// Exact-correlation reference front end for JS8 receive development.
// This is a correctness oracle, not the intended full-band ESP32-P4 implementation.
struct Config {
  float first_hz = 200.0f;
  float bin_spacing_hz = 6.25f;
  uint16_t bin_count = 449;
  uint8_t rows_per_symbol = 2;
};

struct OutputGrid {
  float* cells = nullptr;
  size_t row_capacity = 0;
  size_t stride = 0;
};

// Writes non-negative linear power into caller-owned storage. No allocation.
// Returns rows written. The caller chooses a narrow or wide frequency span.
size_t power_grid(const int16_t* samples, size_t count, Submode submode,
                  const Config& config, const OutputGrid& output);

bool grid_view(float* cells, size_t rows, size_t bins, size_t stride,
               sync::EnergyGrid* out);

bool self_check();

}  // namespace orcsdr::js8::spectral

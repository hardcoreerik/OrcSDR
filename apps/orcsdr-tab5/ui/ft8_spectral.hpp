#pragma once

#include "ft8_mode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::ftx::spectral {

// Largest currently described symbol window is JS8 Slow: 3840 samples at 12 kHz.
// Research-pending JS8 profiles do not become enabled merely because the
// reference analyzer can hold their window length.
constexpr std::size_t kMaxSymbolSamples = 3840;

struct Config {
  uint32_t first_bin_millihz = 200000;
  uint32_t bin_spacing_millihz = 6250;
  uint16_t bin_count = 0;
  uint8_t rows_per_symbol = 1;
};

struct OutputGrid {
  float* cells = nullptr;
  std::size_t row_capacity = 0;
  std::size_t stride = 0;
};

struct ReferenceAccumulator {
  const ModeProfile* profile = nullptr;
  Config config{};
  OutputGrid output{};
  std::array<int16_t, kMaxSymbolSamples> window{};
  std::size_t window_count = 0;
  std::size_t hop_samples = 0;
  std::size_t rows_written = 0;
  uint64_t samples_offered = 0;
  bool active = false;
};

// Exact-correlation streaming reference backend.
//
// This is a correctness oracle, not the final ESP32-P4 implementation. It
// correlates each completed symbol-length PCM window against every requested
// frequency bin and writes linear power to the caller-provided grid. No heap
// allocation occurs in begin()/offer().
bool begin(ReferenceAccumulator* state, const ModeProfile& profile,
           const Config& config, const OutputGrid& output);

bool offer(ReferenceAccumulator* state, const int16_t* samples,
           std::size_t count);

void reset(ReferenceAccumulator* state);

std::size_t rows_written(const ReferenceAccumulator& state);

// Returns the exact frequency represented by a grid bin.
double bin_frequency_hz(const Config& config, std::size_t bin);

bool self_check();

}  // namespace orcsdr::ftx::spectral

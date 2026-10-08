#pragma once

#include "ft8_sync.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::ftx::demod {

constexpr std::size_t kMaxSoftBits = 174;
using SoftBits = std::array<float, kMaxSoftBits>;

// How tone energies become soft metrics. linear_symbol is the production default; the others exist to be measured
// (Task 3, experiment 4) and are selected only by tools until a measurement justifies changing the default.
enum class Metric : uint8_t {
  linear_symbol,      // (max E0 - max E1) / mean E of this symbol
  amplitude_symbol,   // same on sqrt(E)
  linear_frame,       // normalized by the mean E over every data symbol of the candidate
  amplitude_frame,    // same on sqrt(E)
  lse_amplitude,      // log-sum-exp over tones of gain * sqrt(E) / sqrt(mean E): non-max-log
};

struct Config {
  Metric metric = Metric::linear_symbol;
  float llr_gain = 1.0f;
  float max_abs_llr = 16.0f;
};

struct Result {
  SoftBits llr{};
  uint16_t bit_count = 0;
  float mean_symbol_contrast = 0.0f;
};

// Converts candidate-local tone energies into max-log-style soft metrics.
// Positive LLR favors bit 0; negative favors bit 1. Values are dimensionless
// decoder reliability metrics, not SNR or dB.
bool soft_demodulate(const ModeProfile& profile, const sync::EnergyGrid& grid,
                     const sync::Geometry& geometry,
                     const sync::Candidate& candidate, Result* result,
                     const Config& config = {});

bool self_check();

}  // namespace orcsdr::ftx::demod

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
};

struct RawCandidate {
  sync::Candidate spectral{};
  RawFrame frame{};
  DemodStats demod{};
  uint32_t start_sample = 0;
  float base_hz = 0.0f;
};

struct Stats {
  uint16_t candidates_found = 0;
  uint16_t candidates_demodulated = 0;
  uint16_t frames_emitted = 0;
};

size_t extract(const int16_t* samples, size_t sample_count, Submode submode,
               const sync::EnergyGrid& grid, const Config& config,
               RawCandidate* output, size_t capacity, Stats* stats);

bool self_check();

}  // namespace orcsdr::js8::frontend

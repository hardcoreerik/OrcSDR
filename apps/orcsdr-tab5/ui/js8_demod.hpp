#pragma once

#include "js8_frame.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::js8 {

struct DemodConfig {
  size_t start_sample = 0;
  float base_hz = 0.0f;
  float min_sync_score = 0.20f;
  uint8_t min_sync_hits = 14;
};

struct DemodStats {
  float sync_score = 0.0f;
  float mean_margin = 0.0f;
  uint8_t sync_hits = 0;
};

// Candidate-local reference demodulator. It performs no search and allocates no memory:
// caller supplies a candidate start time and tone-0 frequency. The result is raw
// channel tones only; FEC/CRC/message acceptance happens in later JS8 stages.
bool demodulate_tones(const int16_t* samples, size_t count, Submode submode,
                      const DemodConfig& config, RawFrame* frame, DemodStats* stats);

bool self_check_demod();

}  // namespace orcsdr::js8

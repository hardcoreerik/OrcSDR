#pragma once
// Bounded, receive-only sync-pattern classifier for SSTV Auto VIS + Sync.
// Observes 1200 Hz pulses produced by the existing SDR audio detector.
// Never allocates, captures images or touches the display/storage tasks.
#include "sstv_receiver.hpp"
#include <cstdint>

namespace orcsdr::sstv::auto_detect {
struct Candidate {
  Mode mode = Mode::auto_vis;
  uint8_t score = 0; // relative *heuristic*, never a probability
  uint8_t pulses = 0;
  uint32_t sync_start = 0; // 12 kHz sample index; wraps safely
  bool locked = false;
};
void reset();
// Call on the *end* of each observed 1200 Hz sync pulse.
// Result is locked only when >=6 pulses are coherent, with a distinctive
// best candidate. Unknown, noisy and ambiguous signals remain unlocked.
Candidate observe(uint32_t sync_start, uint16_t length_samples);
} // namespace orcsdr::sstv::auto_detect

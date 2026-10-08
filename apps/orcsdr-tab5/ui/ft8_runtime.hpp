#pragma once

#include "ft8_model.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8_runtime {

// Firmware glue for the native receive-only decoder (docs/ft8/AUDIO_TAP_PROPOSAL.md, approved by the owner):
//   DSP task --offer_iq()--> audio tap (own state) --> PSRAM ring of 12 kS/s USB audio
//   low-priority task: at each UTC slot end copies the slot out of the ring, runs the native backend, delivers decodes.
// offer_iq() reads the raw CU8 block and never writes to it; it is called only while the FT8 screen owns the receiver, so
// with the feature off the existing demodulation and audio paths are untouched.

enum class State : uint8_t { stopped, waiting_clock, waiting_signal, listening, decoding, ready, error };

struct Status {
  State state = State::stopped;
  bool tap_running = false;
  uint32_t input_rate_hz = 0;
  uint64_t tap_blocks = 0;
  uint64_t ring_samples = 0;       // total 12 kS/s samples written
  uint32_t slots_decoded = 0;
  uint32_t slots_skipped_incomplete = 0;
  uint32_t last_slot_decodes = 0;
  uint32_t last_decode_ms = 0;     // wall time of the last backend run
  uint32_t last_spectral_ms = 0;
  uint32_t last_refine_ms = 0;
  uint32_t last_gate_ms = 0;
  uint32_t max_block_us = 0;       // worst tap cost per IQ block
  uint32_t avg_block_us = 0;
  bool last_deadline_hit = false;
  uint16_t last_coarse = 0;
};

using DecodeCallback = void (*)(const orcsdr::ft8::Decode& decode, void* context);

// Allocates the ring and backend (PSRAM) and starts the decoder task. `on_decode` runs on the decoder task.
using ClockValidFn = bool (*)();
bool start(orcsdr::ft8::DigitalMode mode, DecodeCallback on_decode, void* context, ClockValidFn clock_valid);
void stop();
// The FT8 screen calls this on every refresh. When it has not been called for several seconds the screen is gone and the
// runtime stops itself, so leaving the screen never needs an explicit hook.
void touch();
bool active();
bool set_mode(orcsdr::ft8::DigitalMode mode);
// Called from the DSP task for every raw block while active. `retuned` forces a tap reset and marks a discontinuity.
void offer_iq(const uint8_t* iq, size_t bytes, uint32_t sample_rate_hz);
// Call on tune, band, gain or rate changes: the audio is no longer contiguous, so the slot in progress is skipped.
void note_discontinuity();
Status status();

}  // namespace orcsdr::ft8_runtime

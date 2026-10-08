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
  orcsdr::ft8::DigitalMode mode = orcsdr::ft8::DigitalMode::ft8;
  bool headless = false;
  uint16_t cfg_k = 0;
  uint16_t cfg_gate = 0;
  uint8_t cfg_fine_rows = 4;
  uint32_t cfg_deadline_ms = 0;
  uint32_t slot_rms = 0;           // RMS of the last decoded slot's audio (int16 counts)
  uint32_t slot_peak = 0;
  uint32_t slot_clipped = 0;       // samples at full scale in that slot
  float dial_offset_hz = 0.0f;     // the offset the tap is applying
};

using DecodeCallback = void (*)(const orcsdr::ft8::Decode& decode, void* context);

// Allocates the ring and backend (PSRAM) and starts the decoder task. `on_decode` runs on the decoder task.
using ClockValidFn = bool (*)();
// Where the dial frequency sits in the baseband, in Hz (dial minus the tuner's actual centre; positive = above it). The tuner
// steps coarsely, so this is rarely zero; the tap shifts its USB passband by it. Polled by the decoder task.
using DialOffsetFn = float (*)();
bool start(orcsdr::ft8::DigitalMode mode, DecodeCallback on_decode, void* context, ClockValidFn clock_valid, DialOffsetFn dial_offset);
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

// A live waterfall of the tap's audio, 200-3000 Hz at 6.25 Hz per bin (one 1920-point FFT every 150 ms, each row normalised to its own
// median so it adapts to any noise floor). Rows are 0-255 (8 counts per dB above the median plus 3 dB, so noise stays dark). Display only: it never feeds the decoder.
constexpr size_t kWaterfallRows = 112;
constexpr size_t kWaterfallBins = 449;
struct WaterfallView {
  const uint8_t* data = nullptr;   // kWaterfallRows x kWaterfallBins, circular
  uint32_t sequence = 0;           // rows produced so far; the newest row is (sequence - 1) % kWaterfallRows
};
WaterfallView waterfall();

// Runs with no FT8 screen (scripting): the runtime stays alive without touch() calls until set_headless(false).
void set_headless(bool headless);
// Decoder tunables, applied from the next slot. `k` = coarse candidates refined, `gate` = candidates sent through the FEC gates, `fine_rows` = fine-grid rows per symbol (4 or 8).
bool set_config(uint16_t k, uint16_t gate, uint8_t fine_rows, uint32_t deadline_ms);
// The audio of the slot most recently decoded (12 kS/s mono), for saving to storage. False if none yet.
bool last_slot_audio(const int16_t** samples, size_t* count, uint64_t* slot_epoch_ms);
// Call when finished with the pointer from last_slot_audio(); until then the copy is not refreshed.
void release_slot_audio();

}  // namespace orcsdr::ft8_runtime

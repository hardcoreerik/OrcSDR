#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::lora_preamble {

// Real-time LoRa preamble detector for the 960 kS/s CU8 stream.
//
// The stream is decimated to the chip rate (250 kS/s for BW 250 kHz), multiplied
// by a reference down-chirp one symbol at a time and Fourier transformed. A LoRa
// preamble is a run of identical up-chirps, so consecutive windows put their
// peak on the same bin (whatever the symbol boundary phase) with roughly
// 10*log10(2^SF) dB of processing gain. Wideband interference (LTE, Wi-Fi) and
// noise do not produce a run of windows with a stable peak bin, so the detector
// is independent of gain, dongle and noise floor, and works well below the
// level an energy trigger can see.

// Allocates and builds the tables for the spreading factor (7..12, BW 250 kHz).
// Idempotent; returns false when memory is unavailable.
bool configure(uint8_t spreading_factor);
bool ready();
void reset();

struct Result {
  bool fired = false;         // a preamble was confirmed during this call
  uint32_t windows = 0;       // symbol windows evaluated during this call
  uint32_t quiet_samples = 0; // input samples since the last window with strong chirp activity
};

// Feed raw CU8 IQ at 960 kS/s. Safe to call with any block size.
Result feed(const uint8_t* iq, size_t bytes);

struct Status {
  uint32_t windows = 0;
  uint32_t fires = 0;
  uint32_t max_window_us = 0;
  uint16_t last_bin = 0;
  float last_ratio = 0;
  uint8_t run = 0;
  uint8_t spreading_factor = 0;
};
Status status();

}  // namespace orcsdr::lora_preamble

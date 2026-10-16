#pragma once
// ACARS receive-chain self-test. Generates a synthetic ACARS signal (AM carrier modulated by a 1200 / 2400 Hz MSK tone, the
// ARINC 618 framing around a fixed test block) as 240 kS/s CU8, and runs it through a private Frontend240k + Decoder.
// It touches neither the live decoder, the dashboard model nor the radio, so it can run beside live reception.
// It proves that this build of the decoder and front end decode a known signal on this hardware; it is not a recording and
// says nothing about sensitivity on real signals. Pure C++, so the same code is host-tested and runs in the firmware.
#include <cstddef>
#include <cstdint>

namespace orcsdr::acars_selftest {

struct Result {
  unsigned cases = 0;
  unsigned passed = 0;          // exactly one block, with the expected tail, label, text and no repair
  unsigned duplicates = 0;      // cases that delivered more than one block
  unsigned missed = 0;          // cases that delivered none
  uint32_t total_us = 0;        // time spent inside the front end and decoder (device clock)
  uint32_t samples = 0;         // complex samples processed
  bool allocated = true;        // false: the working memory could not be allocated, nothing ran
};

// Runs every case: five bit-phase offsets, clean and with noise at sigma 0.05 of full scale. `now_us` supplies a
// microsecond clock (may be null; the timing is then zero).
Result run(uint32_t (*now_us)());

// Streaming generator, public for the host tests. One test block per signal, bit phase set by `lead_in_samples`.
class Signal {
 public:
  static constexpr size_t kMaxBits = 1024;
  Signal(int lead_in_samples, double noise_sigma, uint32_t seed);
  // Writes up to `max_bytes` (even) CU8 bytes; returns the number written, 0 when the signal has ended.
  size_t next(uint8_t* out, size_t max_bytes);
  static const char* tail();     // expected tail of the test block
  static const char* text();     // expected text of the test block
 private:
  float noise();
  bool bits_[kMaxBits]{};
  size_t bit_count_ = 0;
  size_t bit_index_ = 0;
  int sample_in_bit_ = 0;
  int lead_ = 0;
  int trailer_ = 0;
  double noise_sigma_ = 0;
  uint32_t rng_ = 1;
  bool previous_state_ = true;
  double carrier_phase_ = 0, audio_phase_ = 0, tone_step_ = 0;
};

}  // namespace orcsdr::acars_selftest

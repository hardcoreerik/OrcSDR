#pragma once
// Digital Voice channelizer: 240 kS/s unsigned-8-bit IQ -> 48 kS/s complex float, 5:1, for the DMR and NXDN frontends.
// Pure C++, no allocation, no radio: the receiver owner supplies blocks. The wanted channel sits exactly rate/4
// (60 kHz) above the tuned centre so the tuner's DC spike stays out of the channel; the mixer is the exact
// (-j)^n sequence, so no oscillator error is introduced. A retune or gap must call reset().
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dv {

struct Iq48 { float re; float im; };   // same layout as the decoders' own sample types

class Channelizer {
 public:
  static constexpr uint32_t kInputRate = 240000;
  static constexpr uint32_t kOutputRate = 48000;
  static constexpr uint32_t kDecimation = kInputRate / kOutputRate;   // 5
  static constexpr int32_t kChannelOffsetHz = 60000;
  static constexpr size_t kTaps = 65;

  Channelizer() noexcept;
  void reset() noexcept;
  // `pairs` IQ pairs of CU8 (2 bytes each). Writes at most `capacity` outputs; returns the number written.
  // capacity must be >= pairs / 5 + 1 or input is dropped (counted in dropped_inputs()).
  size_t process_cu8(const uint8_t* cu8, size_t pairs, Iq48* out, size_t capacity) noexcept;
  uint64_t dropped_inputs() const noexcept { return dropped_; }
  // Relative RMS (0..~1.4) of the channel samples produced since the last call; 0 when none. Not calibrated.
  float take_level() noexcept;

 private:
  std::array<float, kTaps> taps_{};
  std::array<float, kTaps> hist_re_{}, hist_im_{};
  size_t pos_ = 0;
  uint8_t phase_ = 0;       // n mod 4 of the mixer
  uint8_t decim_ = 0;       // inputs since the last output
  uint64_t dropped_ = 0;
  double power_sum_ = 0;
  uint32_t power_n_ = 0;
};

// 48 kS/s -> 24 kS/s complex, 2:1, for the NXDN48 frontend (6.25 kHz channel; halves its per-sample cost). 31-tap Hamming
// low-pass, cutoff 9 kHz, unity DC gain. State persists across calls; reset() on any retune/discontinuity.
class Decimator2 {
 public:
  static constexpr size_t kTaps = 31;
  Decimator2() noexcept;
  void reset() noexcept;
  // Returns outputs written (<= count/2 + 1; capacity must cover that).
  size_t process(const Iq48* in, size_t count, Iq48* out, size_t capacity) noexcept;

 private:
  std::array<float, kTaps> taps_{};
  std::array<float, kTaps> hist_re_{}, hist_im_{};
  size_t pos_ = 0;
  uint8_t phase_ = 0;
};

}  // namespace orcsdr::dv

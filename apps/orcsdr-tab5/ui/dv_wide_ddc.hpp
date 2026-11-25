#pragma once
// Digital Voice wide-stream digital tuner: 2.4 MS/s unsigned-8-bit IQ -> 240 kS/s complex float, with the wanted
// frequency moved to exactly rate/4 (+60 kHz) so the existing dv::Channelizer (240k -> 48k) and the decoders behind it
// are used unchanged. Retuning inside the wide span is changing one number: no radio retune, no stop, no rate change.
//
// Built for the P4's cost, not for elegance: the mixer is an integer table lookup (frequency steps of 1.25 kHz), the
// first decimation (5:1) is a 3-stage CIC of integer adds on the mixed samples, and only the second decimation (2:1,
// 17 taps) runs in float at 480 kS/s. Pure C++, no allocation, no radio. A retune of the *radio* or any gap: reset().
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dv {

struct Iq240 { float re; float im; };

class WideDdc {
 public:
  static constexpr uint32_t kInputRate = 2400000;
  static constexpr uint32_t kOutputRate = 240000;
  static constexpr uint32_t kDecimation = kInputRate / kOutputRate;   // 10 = 5 (CIC) x 2 (FIR)
  static constexpr int32_t kChannelOffsetHz = 60000;                  // where the wanted channel lands (matches Channelizer)
  static constexpr int32_t kGridHz = 1250;                            // mixer frequency step (inputRate / kTable)
  static constexpr size_t kTable = kInputRate / kGridHz;              // 1920 entries
  static constexpr size_t kFirTaps = 17;

  WideDdc() noexcept;
  void reset() noexcept;
  // Wanted frequency relative to the radio's centre, in Hz (|offset| well inside +-1.2 MHz); rounded to the nearest
  // 1.25 kHz. Takes effect at the next block; the filter state is kept (clean after the filters settle, ~20 inputs).
  void set_offset_hz(int32_t offset_hz) noexcept;
  int32_t offset_hz() const noexcept { return offset_hz_; }
  // `pairs` IQ pairs of CU8. Writes at most `capacity` outputs (needs pairs/10 + 1); returns the number written.
  size_t process_cu8(const uint8_t* cu8, size_t pairs, Iq240* out, size_t capacity) noexcept;
  uint64_t dropped_inputs() const noexcept { return dropped_; }

 private:
  std::array<int16_t, 2 * kTable> table_{};    // cos, sin of 2*pi*m/kTable in Q15, interleaved
  std::array<float, kFirTaps> taps_{};
  std::array<float, kFirTaps> hist_re_{}, hist_im_{};
  size_t pos_ = 0;
  uint8_t half_ = 0;                            // CIC outputs since the last FIR output
  // CIC (3 integrators at the input rate, 3 combs at the output rate); unsigned so wraparound is defined.
  uint32_t i1r_ = 0, i2r_ = 0, i3r_ = 0, i1i_ = 0, i2i_ = 0, i3i_ = 0;
  uint32_t d1r_ = 0, d2r_ = 0, d3r_ = 0, d1i_ = 0, d2i_ = 0, d3i_ = 0;
  uint8_t cic_phase_ = 0;
  uint32_t index_ = 0;                          // mixer table position
  uint32_t step_ = 0;                           // table positions per input sample
  int32_t offset_hz_ = 0;
  uint64_t dropped_ = 0;
};

}  // namespace orcsdr::dv

#pragma once
// 51.2 kS/s mono audio -> 48 kS/s (ratio 15/16), cubic (Catmull-Rom) interpolation. The demodulators write 51.2 kHz audio
// when the stream is 2.56 MS/s; this brings it to the 48 kHz everything downstream (speaker, recorder, web audio, meters)
// expects. In place is safe: it never writes ahead of what it has read. State carries across calls; reset() on any gap.
#include <cstddef>
#include <cstdint>

namespace orcsdr::audio_rate_conv {

class Down16to15 {
 public:
  void reset() noexcept;
  // Converts `count` samples from `in` to `out` (out may equal in); returns the samples written (<= count).
  size_t process(const int16_t* in, size_t count, int16_t* out) noexcept;

 private:
  float w_[4] = {0, 0, 0, 0};   // x[i-1], x[i], x[i+1], x[i+2]
  uint8_t filled_ = 0;
  uint8_t shifts_needed_ = 0;
  uint8_t frac_ = 0;            // position between w_[1] and w_[2], in 1/15 of a sample
};

}  // namespace orcsdr::audio_rate_conv

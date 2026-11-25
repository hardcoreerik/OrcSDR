#include "audio_rate_conv.hpp"

namespace orcsdr::audio_rate_conv {

void Down16to15::reset() noexcept {
  w_[0] = w_[1] = w_[2] = w_[3] = 0.0f;
  filled_ = 0;
  shifts_needed_ = 0;
  frac_ = 0;
}

size_t Down16to15::process(const int16_t* in, size_t count, int16_t* out) noexcept {
  size_t written = 0;
  for (size_t n = 0; n < count; ++n) {
    w_[0] = w_[1]; w_[1] = w_[2]; w_[2] = w_[3]; w_[3] = static_cast<float>(in[n]);
    if (filled_ < 4) {
      if (++filled_ < 4) continue;
      shifts_needed_ = 0;
    } else if (shifts_needed_ > 0) {
      --shifts_needed_;
    }
    if (shifts_needed_ != 0) continue;
    const float t = static_cast<float>(frac_) * (1.0f / 15.0f);
    const float y = w_[1] + 0.5f * t * (w_[2] - w_[0] +
                    t * (2.0f * w_[0] - 5.0f * w_[1] + 4.0f * w_[2] - w_[3] +
                    t * (3.0f * (w_[1] - w_[2]) + w_[3] - w_[0])));
    out[written++] = static_cast<int16_t>(y > 32767.0f ? 32767 : y < -32768.0f ? -32768 : static_cast<int32_t>(y < 0 ? y - 0.5f : y + 0.5f));
    // advance 16/15 of an input sample: one shift, plus a second when the fraction wraps
    if (frac_ + 1 >= 15) { shifts_needed_ = 2; frac_ = static_cast<uint8_t>(frac_ + 1 - 15); }
    else { shifts_needed_ = 1; frac_ = static_cast<uint8_t>(frac_ + 1); }
  }
  return written;
}

}  // namespace orcsdr::audio_rate_conv

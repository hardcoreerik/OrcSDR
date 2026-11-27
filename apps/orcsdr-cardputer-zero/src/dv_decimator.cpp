#include "dv_decimator.hpp"

#include <algorithm>

namespace orc::zero::dv {

void CicDecimator::reset() noexcept {
  for (unsigned s = 0; s < kStages; ++s) {
    integ_re_[s] = integ_im_[s] = 0;
    comb_re_[s] = comb_im_[s] = 0;
  }
  phase_ = 0;
}

size_t CicDecimator::process_cu8(const std::uint8_t* cu8, size_t pairs, std::uint8_t* out,
                                 size_t capacity) noexcept {
  if (!cu8 || !out) return 0;
  size_t written = 0;
  for (size_t n = 0; n < pairs; ++n) {
    // CU8 is offset binary: 128 is zero.
    std::int64_t re = std::int64_t(cu8[n * 2 + 0]) - 128;
    std::int64_t im = std::int64_t(cu8[n * 2 + 1]) - 128;

    // Integrator section, every input sample.
    for (unsigned s = 0; s < kStages; ++s) {
      integ_re_[s] += re;
      integ_im_[s] += im;
      re = integ_re_[s];
      im = integ_im_[s];
    }

    if (++phase_ < kRate) continue;
    phase_ = 0;

    // Comb section, once per output sample.
    for (unsigned s = 0; s < kStages; ++s) {
      const std::int64_t previous_re = comb_re_[s], previous_im = comb_im_[s];
      comb_re_[s] = re;
      comb_im_[s] = im;
      re -= previous_re;
      im -= previous_im;
    }

    if (written >= capacity) continue;  // keep state coherent even when truncated
    // Back to CU8: divide by the CIC gain, re-bias, saturate.
    const std::int64_t scaled_re = (re >> kGainShift) + 128;
    const std::int64_t scaled_im = (im >> kGainShift) + 128;
    out[written * 2 + 0] = std::uint8_t(std::max<std::int64_t>(0, std::min<std::int64_t>(255, scaled_re)));
    out[written * 2 + 1] = std::uint8_t(std::max<std::int64_t>(0, std::min<std::int64_t>(255, scaled_im)));
    ++written;
  }
  return written;
}

}  // namespace orc::zero::dv

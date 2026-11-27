#pragma once
// Anti-alias decimator for the DMR receive path: CU8 at 2.4 MS/s -> CU8 at
// 240 kS/s (R = 10), which the DV channeliser then takes to 48 kS/s.
//
// Why this exists: probing live RF by keeping every 10th sample raised the level
// but acquired nothing. Subsampling without filtering folds everything above
// 120 kHz back into the channel - a 700 kHz signal lands at 220 kHz, inside the
// pass band, and buries the DMR bursts.
//
// A CIC is used rather than an FIR because the cost matters on this CPU: N
// integrator adds per input sample and N comb subtractions per output, with no
// multiplies. R = 10, N = 3 gives nulls at multiples of fs/R = 240 kHz and about
// -1.9 dB droop at 50 kHz, which is flat enough across a 12.5 kHz channel.
#include <cstddef>
#include <cstdint>

namespace orc::zero::dv {

class CicDecimator {
 public:
  static constexpr unsigned kStages = 3;
  // The RTL cannot sustain 2.4 MS/s on this Pi over USB without read failures,
  // so the rate is a parameter: R=10 for 2.4 MS/s, R=4 for the 960 kS/s that the
  // rest of the application already streams reliably.
  explicit CicDecimator(unsigned rate = 10) noexcept : rate_(rate ? rate : 1) {
    gain_shift_ = 0;
    unsigned gain = 1;
    for (unsigned s = 0; s < kStages; ++s) gain *= rate_;
    while ((1u << gain_shift_) < gain) ++gain_shift_;
  }

  unsigned rate() const noexcept { return rate_; }
  void reset() noexcept;

  // `pairs` CU8 IQ pairs. Writes at most pairs/kRate + 2 pairs to `out`.
  // Returns the number of pairs written.
  size_t process_cu8(const std::uint8_t* cu8, size_t pairs, std::uint8_t* out,
                     size_t capacity) noexcept;

 private:
  std::int64_t integ_re_[kStages]{}, integ_im_[kStages]{};
  std::int64_t comb_re_[kStages]{}, comb_im_[kStages]{};
  unsigned phase_ = 0;
  unsigned rate_ = 10;
  unsigned gain_shift_ = 10;
};

}  // namespace orc::zero::dv

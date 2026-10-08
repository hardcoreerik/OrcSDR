#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::ftx::audio_tap {

// Weak-signal analysis tap (candidate B of docs/ft8/AUDIO_TAP_PROPOSAL.md): a sidecar that reads the raw CU8 receive block and
// owns all of its own state. It never edits the block and never touches the speaker/web/recorder audio path.
//
//   CU8 at N x 240 kS/s -> CIC3 decimation to 240 kS/s -> DC removal -> mix the USB passband centre (1600 Hz) to 0 Hz
//   -> 41-tap FIR /5 -> 447-tap FIR /4 (12 kS/s, passband +-1450 Hz, opposite sideband stopped from +-1900 Hz)
//   -> mix back up by 1600 Hz -> real int16 PCM at 12 kS/s.
//
// Frequency convention: the radio is tuned to the dial frequency; a signal f Hz ABOVE the dial (baseband +f) appears as an
// audio tone at f Hz (upper sideband); one below the dial is rejected. No AGC, no limiter, no de-emphasis: the only gain is
// the fixed numeric scale below, so relative tone and noise levels reach the decoder untouched.
constexpr uint32_t kOutputRateHz = 12000;
constexpr uint32_t kBaseRateHz = 240000;
constexpr float kOutputScale = 64.0f;     // int16 counts per CU8 count at 0 dB passband gain
constexpr size_t kStageATaps = 41;
constexpr size_t kStageBTaps = 447;

struct Metrics {
  uint64_t input_samples = 0;     // complex samples consumed
  uint64_t output_samples = 0;
  uint32_t blocks = 0;
  uint32_t clipped_outputs = 0;   // int16 saturations
};

struct Tap {
  uint32_t input_rate_hz = 0;
  uint32_t cic_ratio = 0;                       // input_rate / 240 kS/s
  float inv_cic_gain = 1.0f;

  std::array<float, kStageATaps> taps_a{};
  std::array<float, kStageBTaps> taps_b{};

  // CIC3 (integers, wraps harmlessly modulo 2^32) per channel
  int32_t integ[2][3]{};
  int32_t comb[2][3]{};
  uint32_t cic_phase = 0;
  float dial_offset_hz = 0.0f;                  // where the dial frequency sits in the baseband (dial minus the tuner centre)

  float dc[2]{};

  float mix1_cos = 1.0f, mix1_sin = 0.0f;       // 240 kS/s mixer state (float: the P4 FPU is single precision)
  float mix2_cos = 1.0f, mix2_sin = 0.0f;       // 12 kS/s mixer state
  uint32_t mix1_count = 0;
  uint32_t mix2_count = 0;

  // Double-length histories (each sample written twice) so every convolution reads one contiguous run, no modulo.
  std::array<float, 2 * kStageATaps> hist_a[2]{};
  size_t head_a = 0;
  uint32_t phase_a = 0;

  std::array<float, 2 * kStageBTaps> hist_b[2]{};
  size_t head_b = 0;
  uint32_t phase_b = 0;

  Metrics metrics{};
};

// Rates the tap accepts: a multiple of 240 kS/s (240 k, 480 k, 960 k, 1.2 M, 2.4 M, ...).
bool rate_supported(uint32_t input_rate_hz);
bool begin(Tap* tap, uint32_t input_rate_hz);
// The receiver rarely sits exactly on the dial frequency (the tuner steps coarsely and the app may offset it). The tap needs
// the dial's position in the baseband: positive when the dial is above the tuner centre. Keeps the filter state.
void set_dial_offset(Tap* tap, float dial_offset_hz);
// Clears filter state and counters (use on retune, rate change, a dropped block, or receiver stop).
void reset(Tap* tap);
// Upper bound on the output samples produced from `bytes` of CU8 (2 bytes per complex sample).
size_t max_output_samples(const Tap& tap, size_t bytes);
// Consumes whole complex samples and appends int16 PCM to `out`. Returns the number written. Never writes beyond
// `capacity`: samples that would not fit are dropped and counted by the caller through the return value being smaller than
// max_output_samples().
size_t process_cu8(Tap* tap, const uint8_t* iq, size_t bytes, int16_t* out, size_t capacity);

bool self_check();

}  // namespace orcsdr::ftx::audio_tap

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Channel selection, FIR anti-aliasing and integer-rate decimation before
// native VDL2 demodulator. No RX RRC added (ETSI specifies full RC TX).
#include "vdl2_receiver.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace orcsdr::vdl2 {
class Channel {
public:
  Channel();
  // First supported RF rates: 1050000 -> 105000 (exact 10 sps);
  // 250000 -> 50000 (fractional 4.7619 sps). 50k/105k for offline fixtures.
  bool configure(uint32_t input_sps,uint32_t channel_sps,int32_t offset_hz=0);
  void process_cu8(const uint8_t* input,size_t complex_count);
  void process_complex(const ComplexSample* input,size_t complex_count);
  Receiver& receiver() { return rx_; }
  const Receiver& receiver()const { return rx_; }
private:
  static constexpr size_t kTaps=127;
  std::array<float,kTaps> taps_{};
  std::array<ComplexSample,kTaps> delay_{};
  size_t delay_index_=0;
  unsigned phase_=0, decimation_=1;
  float mix_i_=1.f,mix_q_=0.f,step_i_=1.f,step_q_=0.f;
  int32_t offset_=0;
  uint32_t input_rate_=105000;
  uint32_t mixer_ticks_=0;
  Receiver rx_{};
};
} // namespace orcsdr::vdl2

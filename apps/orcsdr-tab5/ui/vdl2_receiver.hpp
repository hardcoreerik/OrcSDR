// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// VDL Mode 2 receive-only 8DPSK baseband receiver. Input = channel-centered
// complex IQ at 50k or 105k samples/s (externally channel-filtered, not TX
// RRC-matched). Native header/descrambler/RS/AVLC decoding in C++17.
// No heap allocation in the sample loop. Run on owning DSP task, not UI task.
#include "vdl2_rs.hpp"
#include "vdl2_frames.hpp"
#include "vdl2_burst.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace orcsdr::vdl2 {
struct ComplexSample { float i=0, q=0; };
struct ReceiverStats {
  uint32_t uw_candidates=0;
  uint32_t uw_locked=0;
  uint32_t header_fail=0;
  uint32_t header_ok=0;
  uint32_t fec_fail=0;
  uint32_t fec_ok=0;
  uint32_t bursts_completed=0;
  uint32_t dropped=0;
  float last_cfo_hz=0;
  float last_preamble_cost=0;
};
class Receiver {
public:
  explicit Receiver(uint32_t channel_rate=105000);
  bool set_rate(uint32_t rate); // allowed 50k, 105k; resets demod
  void reset();
  void set_callback(FrameCallback cb, void* context) { frames_.set_callback(cb,context); }
  void set_frequency(uint32_t hz) { frames_.set_frequency(hz); }
  void process_sample(float i,float q);
  void process_cu8(const uint8_t* iq,size_t complex_count);
  const ReceiverStats& stats() const { return stats_; }
  const Stats& frames_stats() const { return frames_.stats(); }
private:
  static constexpr size_t kRingSize=512;
  static constexpr size_t kBitBytes=(25u+kMaxVdl2TransmittedBits+7u)/8u;
  static constexpr size_t kTxBytes=(kMaxVdl2TransmittedBits+7u)/8u;
  enum class Stage:uint8_t { hunt, collect };
  ComplexSample sample(double position) const;
  bool correlate(double pos,float& metric);
  bool refine(double pos,double& best_pos,float& theta,float& cost);
  void collect_available();
  void finish_burst();
  bool put_received_bit(uint8_t bit);
  void rewind_hunt();
  float ref_phase_[16]{};
  ComplexSample expected_[16]{};
  std::array<ComplexSample,kRingSize> ring_{};
  size_t sample_count_=0;
  double cursor_=0, symbol_step_=10, next_symbol_=0, uw_start_=0;
  float theta_=0, initial_theta_=0, noise_=1e-6f;
  ComplexSample previous_{};
  Stage stage_=Stage::hunt;
  Descrambler15 descrambler_{};
  std::array<uint8_t,kBitBytes> received_{};
  std::array<uint8_t,kTxBytes> transmitted_{};
  std::array<uint8_t,(kMaxVdl2DataBits+7u)/8u> recovered_{};
  size_t received_bits_=0, needed_bits_=0;
  uint32_t channel_rate_=105000;
  ReceiverStats stats_{};
  FrameDecoder frames_{};
  RsCodec rs_{};
};
} // namespace orcsdr::vdl2

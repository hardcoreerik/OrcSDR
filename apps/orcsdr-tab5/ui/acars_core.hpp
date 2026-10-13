// SPDX-License-Identifier: AGPL-3.0-only
// OrcSDR ACARS receive core — independently authored implementation.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::acars {
constexpr unsigned kChannelRate = 24000;
constexpr unsigned kBitRate = 2400;
constexpr size_t kMaxBlockBytes = 250;

struct Message {
  uint32_t frequency_hz = 0;
  char tail[8]{};        // 7 chars + NUL
  char flight[7]{};      // if downlink message has flight ID
  char label[3]{};
  char mode = 0;
  char acknowledgement = 0;
  char block_id = 0;
  bool has_text = false;
  // ARINC 618: ETB (0x17) intermediate block, ETX (0x03) final block.
  // Preserve the received, CRC-verified delimiter for later reassembly.
  uint8_t end_marker = 0;
  bool corrected = false;
  uint8_t corrected_bits = 0;
  uint16_t raw_bytes = 0;
  uint16_t text_length = 0;
  char text[kMaxBlockBytes]{};
};

struct Stats {
  uint32_t sync_candidates = 0;
  uint32_t crc_ok = 0;
  uint32_t corrected = 0;
  uint32_t crc_rejected = 0;
  uint32_t framing_rejected = 0;
  uint32_t overflows = 0;
  uint32_t bits = 0;
};
using MessageCallback = void(*)(void*, const Message&);

uint16_t crc16_kermit(const uint8_t* bytes, size_t count);
uint8_t odd_parity_byte(uint8_t seven_bit);
// Pure, no dynamic allocation. Accepts raw parity-bearing mode...ETX + 2 BCS bytes.
// Returns true when valid or after a verified 1-bit repair, or a verified
// 2-bit repair localized by exactly two odd-parity failures. No guessing.
bool validate_or_repair(uint8_t* bytes, size_t count,
                                bool& corrected, uint8_t& corrected_bits);

class FrameDecoder {
 public:
  void reset();
  void set_callback(MessageCallback cb, void* context) { callback_ = cb; context_ = context; }
  void set_frequency(uint32_t hz) { frequency_hz_ = hz; }
  void push_bit(bool bit);
  const Stats& stats() const { return stats_; }
 private:
  bool emit();
  void drop(bool overflow = false);
  void consume_byte(uint8_t value);
  enum class State : uint8_t { hunt, collect, crc_first, crc_second, terminator };
  State state_ = State::hunt;
  uint32_t sync_ = 0;
  uint16_t prekey_run_ = 0;
  uint8_t prekey_window_ = 0;
  uint8_t polarity_ = 0;
  uint8_t partial_ = 0;
  uint8_t bit_index_ = 0;
  std::array<uint8_t,kMaxBlockBytes> raw_{};
  uint16_t size_ = 0;
  uint32_t frequency_hz_ = 0;
  Stats stats_{};
  MessageCallback callback_ = nullptr;
  void* context_ = nullptr;
};

// Channel-level decoder: input 24 kS/s complex baseband of AM carrier.
// The AM-envelope/MSK chain never processes raw wideband rates.
// Works in bounded, allocation-free streaming operation. An IQ discontinuity
// must reset the decoder (it intentionally does not guess missing samples).
class Decoder {
 public:
  void reset();
  void set_callback(MessageCallback cb, void* ctx) { frames_.set_callback(cb,ctx); }
  void set_frequency(uint32_t hz) { frames_.set_frequency(hz); }
  void process_channel_iq(float i, float q);
  const Stats& stats() const { return frames_.stats(); }
  FrameDecoder& frame_decoder() { return frames_; }
 private:
  FrameDecoder frames_{};
  float dc_ = 0.f;
  float i1_ = 0.f, q1_ = 0.f;
  float i2_ = 0.f, q2_ = 0.f;
  float pi_ = 0.f, pq_ = 0.f;
  float mixer_i_ = 1.f, mixer_q_ = 0.f;
  int osc_phase_ = 0;
  int bit_phase_ = 0;
  float accum_ = 0.f;
  bool differential_state_ = true;
  bool signal_initialized_ = false;
};

// Budget-oriented front end for default 2.4 MS/s CU8 IQ. Channel tuning is
// relative to the device center; set channel offset within sampled Nyquist.
// Independent two-stage anti-alias FIR and /20 then /5 decimation to 24 kHz.
class Frontend2400k {
 public:
  void reset();
  void set_channel_offset_hz(int32_t hz);
  // Receives interleaved CU8 pairs. Output callbacks delivered at 24 kS/s.
  void process_cu8(const uint8_t* iq, size_t complex_count,
                   void(*emit)(void*,float,float), void* ctx);
 private:
  struct Complex { float i=0, q=0; };
  static constexpr int kStage1Len = 101;
  static constexpr int kStage2Len = 81;
  std::array<Complex,kStage1Len> ring1_{};
  std::array<Complex,kStage2Len> ring2_{};
  int pos1_=0, pos2_=0, count1_=0, count2_=0;
  float osc_i_=1.f, osc_q_=0.f, step_i_=1.f, step_q_=0.f;
  uint32_t renorm_=0;
  std::array<float,kStage1Len> taps1_{};
  std::array<float,kStage2Len> taps2_{};
  bool taps_ready_=false;
  void design_taps();
};
}

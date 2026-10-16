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
//
// Bit timing: there is no clock recovery. The ten possible bit boundaries within a 10-sample bit
// are decoded in parallel, one FrameDecoder each; every CRC-verified block is delivered once (the
// neighbouring phases that also recover it are suppressed as duplicates). A phase that is off by
// half a bit simply never produces a verified block.
class Decoder {
 public:
  static constexpr int kSamplesPerBit = 10;
  Decoder() { reset(); }
  Decoder(const Decoder&) = delete;             // the phase decoders call back into this object
  Decoder& operator=(const Decoder&) = delete;
  void reset();                                 // everything, including the unique-block counters
  // Drops all signal and framing state (a stream gap, a retune) but keeps every counter, so crc_ok, corrected and the
  // other statistics stay in one reset scope ("since decoder start").
  void reset_stream();
  void set_callback(MessageCallback cb, void* ctx) { callback_ = cb; context_ = ctx; }
  void set_frequency(uint32_t hz);
  void process_channel_iq(float i, float q);
  // crc_ok / corrected count unique blocks. The other counters are those of the busiest phase, so
  // they describe one decoder, not ten.
  const Stats& stats() const;
 private:
  static void on_phase_message(void* self, const Message& message);
  void accept(const Message& message);
  std::array<FrameDecoder, kSamplesPerBit> frames_{};
  std::array<float, kSamplesPerBit> accum_{};
  std::array<bool, kSamplesPerBit> state_{};
  MessageCallback callback_ = nullptr;
  void* context_ = nullptr;
  mutable Stats stats_{};
  uint32_t unique_ok_ = 0;
  uint32_t unique_corrected_ = 0;
  uint32_t sample_index_ = 0;
  uint32_t last_hash_ = 0;
  uint32_t last_hash_sample_ = 0;
  bool have_last_ = false;
  float dc_ = 0.f;
  float i1_ = 0.f, q1_ = 0.f;
  float i2_ = 0.f, q2_ = 0.f;
  float pi_ = 0.f, pq_ = 0.f;
  float mixer_i_ = 1.f, mixer_q_ = 0.f;
  int osc_phase_ = 0;
  int bit_phase_ = 0;
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

// Front end for the 240 kS/s CU8 stream the dashboard asks the dongle for (the same low rate the FT8 and SSTV receivers use:
// ten times less USB and DSP load than 2.4 MS/s). One channel: the wanted carrier must sit at +60 kHz (= fs/4) from the tuned
// centre, which keeps it away from the tuner's DC spike and makes the mixer to DC multiplication-free (a rotation by
// 90 degrees per sample). The FIR then low-passes and decimates by 10 to 24 kS/s.
// Bounded, allocation-free, one FIR evaluation per output sample, input processed in cache-sized chunks.
class Frontend240k {
 public:
  static constexpr unsigned kInputRate = 240000;
  static constexpr int32_t kCarrierOffsetHz = 60000;   // carrier position relative to the tuned centre
  static constexpr int kTaps = 129;
  static constexpr int kDecimation = 10;
  static constexpr int kChunk = 480;                   // input samples per pass (a multiple of kDecimation)
  Frontend240k() { reset(); }
  void reset();
  void process_cu8(const uint8_t* iq, size_t complex_count, void(*emit)(void*,float,float), void* ctx);
 private:
  void design_taps();
  std::array<float,kTaps/2+1> taps_{};
  std::array<float,kTaps-1+kChunk> work_i_{};          // [history of kTaps-1 | chunk], contiguous so the FIR never wraps
  std::array<float,kTaps-1+kChunk> work_q_{};
  std::array<float,256> lut_{};                        // CU8 byte -> float in [-1, 1]
  int phase_ = 0;                                      // inputs since the last output (0..kDecimation-1)
  int rotation_ = 0;                                   // position in the 4-step mixer cycle
  bool tables_ready_ = false;
};
}

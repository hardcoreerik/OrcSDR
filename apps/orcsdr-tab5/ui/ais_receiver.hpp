#pragma once
// Original AIS receive-only CU8 IQ front-end and HDLC/FCS validation.
// Input: 240 kS/s complex unsigned IQ, tuned to 162.000 MHz. AIS1/2 at +/-25 kHz.
// The caller owns SDR tuning, session generation, and thread/snapshot handoff.
// No heap allocation, M5GFX, USB driver, network or ESP-IDF dependencies.
#include <cstddef>
#include <cstdint>

namespace orcsdr::ais_rx {
constexpr uint32_t kInputRateHz = 240000;
constexpr uint32_t kCenterHz = 162000000;
constexpr uint32_t kChannelHz[2] = {161975000, 162025000};
constexpr uint32_t kOutputRateHz = 48000;
constexpr uint32_t kSymbolRate = 9600;
constexpr size_t kMaxPayloadBits = 1008;
constexpr size_t kMaxPayloadBytes = 126;

struct Packet {
  uint8_t channel = 0; // 1 = AIS1 / 161.975 MHz, 2 = AIS2 / 162.025 MHz
  uint16_t bit_count = 0; // payload excluding FCS; byte aligned
  uint8_t bytes[kMaxPayloadBytes]{}; // original MSB-first field bytes
};
using PacketCallback = void (*)(const Packet&, void* context);

struct ChannelStats {
  uint64_t candidate_frames = 0;
  uint64_t crc_ok = 0;
  uint64_t crc_failed = 0;
  uint64_t bad_stuffing = 0;
  uint64_t invalid_length = 0;
  uint64_t overflow = 0;
  uint64_t duplicates = 0;
  uint64_t discriminator_samples = 0;
};
struct Stats {
  uint64_t iq_samples = 0;
  uint64_t bad_blocks = 0;
  ChannelStats channels[2]{};
};

// Bit-level receive path, independently usable for deterministic framing tests.
// Feed already sliced GMSK/NRZI *levels*, not decoded HDLC bits.
class Hdlc {
 public:
  void reset();
  void process_level(bool level, uint8_t channel, PacketCallback callback,
                     void* context);
  const ChannelStats& stats() const { return stats_; }
 private:
  static constexpr size_t kMaxRawBits = 1400;
  uint8_t raw_[kMaxRawBits]{};
  uint16_t count_ = 0;
  uint8_t window_ = 0;
  uint8_t window_count_ = 0;
  bool in_frame_ = false;
  bool previous_level_ = false, level_primed_ = false;
  ChannelStats stats_{};
  void finish(uint8_t channel, PacketCallback callback, void* context);
};

class Receiver {
 public:
  Receiver();
  void reset();
  // Explicit discontinuity: resets all DSP/frame histories (e.g. after dropped IQ/retune).
  void discontinuity();
  // Called only from the DSP owner/task. CU8 block size must be even; no internal queue.
  // Returns false if block is null (with nonzero bytes) or not whole IQ pairs.
  bool process_cu8(const uint8_t* bytes, size_t length, uint32_t sample_rate,
                   PacketCallback callback, void* context);
  Stats stats() const;
 private:
  static constexpr size_t kTaps = 63;
  static constexpr size_t kPhases = 5;
  struct Complex { float i = 0, q = 0; };
  struct Channel {
    Complex fir[kTaps]{};
    unsigned fir_pos = 0;
    Complex prev{};
    bool have_prev = false;
    float dc = 0;
    float smooth[3]{};
    unsigned smooth_pos = 0;
    Hdlc hypotheses[kPhases]{};
    uint64_t output_index = 0;
    uint64_t last_packet_index = 0;
    uint32_t last_hash = 0;
    bool have_hash = false;
    ChannelStats aggregate{};
  };
  Channel channels_[2]{};
  float taps_[kTaps]{};
  Complex osc_[2][48]{};
  uint64_t sample_index_ = 0, phase_index_ = 0, bad_blocks_ = 0;
  PacketCallback callback_ = nullptr;
  void* context_ = nullptr;
  struct CallContext { Receiver* owner; unsigned channel; } call_context_[2]{};
  void process_channel_sample(unsigned channel, Complex sample);
  static void on_packet(const Packet& packet, void* context);
  void accepted(unsigned channel, const Packet& packet);
};
} // namespace orcsdr::ais_rx

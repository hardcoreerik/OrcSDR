// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// VDL2 AVLC bit deframer and ACARS-over-AVLC extraction.
// Input: post-FEC, post-descrambling bits (LSB-first in each AVLC octet).
// No memory allocation or radio/hardware dependencies. A callback borrows its data.
#include <array>
#include <cstddef>
#include <cstdint>
#include "acars_core.hpp"

namespace orcsdr::vdl2 {
constexpr size_t kMaxAvlcBytes = 2048; // IMPLEMENTATION CAP, not a protocol constant.
constexpr size_t kMaxStuffedBytes = kMaxAvlcBytes + (kMaxAvlcBytes / 5u) + 16u; // HDLC worst-case 0 insertion after each 5 payload bits, plus slack
constexpr uint32_t kCommonSignallingHz = 136975000u;

struct FrameView {
  const uint8_t* raw = nullptr; // AVLC frame excluding flags, INCLUDING two FCS octets
  size_t raw_size = 0;
  uint32_t source = 0; // 28-bit AVLC addresses (includes address-type/status fields)
  uint32_t destination = 0;
  uint8_t control = 0;
  bool is_acars = false; // true only after inner ACARS CRC and grammar check
  const acars::Message* acars_message = nullptr; // valid only during callback
};
using FrameCallback = void (*)(void*, const FrameView&);
struct Stats {
  uint32_t flags = 0;
  uint32_t frames_crc_ok = 0;
  uint32_t frames_crc_bad = 0;
  uint32_t malformed = 0;
  uint32_t overflow = 0;
  uint32_t acars_ok = 0;
  uint32_t acars_bad = 0;
  uint32_t raw_frames = 0;
};

// Bit-reflected CRC-16/ISO-HDLC / X.25 (poly 0x1021, reflected 0x8408).
uint16_t avlc_crc(const uint8_t* bytes, size_t count, uint16_t initial = 0xffffu);
// ACARS-over-AVLC BCS as used by libacars (same reflected polynomial, init 0).
uint16_t acars_crc(const uint8_t* bytes, size_t count);

class FrameDecoder {
 public:
  void reset(); // clears stream state; counters persist
  void set_callback(FrameCallback callback, void* context) { callback_ = callback; context_ = context; }
  void set_frequency(uint32_t hz) { frequency_hz_ = hz; }
  void push_bit(bool bit);
  // Fully de-stuffed AVLC frame bytes with FCS, for host tests / pre-decoded input.
  bool accept_frame(const uint8_t* bytes, size_t count);
  const Stats& stats() const { return stats_; }
 private:
  void complete();
  bool extract_acars(const uint8_t* bytes, size_t count, acars::Message& out) const;
  static uint32_t addr(const uint8_t* bytes);
  std::array<uint8_t, kMaxStuffedBytes> bits_{}; // PACKED; stuffed bits can exceed 2048-byte payload cap
  std::array<uint8_t, kMaxAvlcBytes> frame_{};
  size_t bit_count_ = 0;
  uint8_t flag_window_ = 0;
  bool in_frame_ = false;
  uint32_t frequency_hz_ = kCommonSignallingHz;
  FrameCallback callback_ = nullptr;
  void* context_ = nullptr;
  Stats stats_{};
};
} // namespace orcsdr::vdl2

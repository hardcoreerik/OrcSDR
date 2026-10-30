// SPDX-License-Identifier: AGPL-3.0-only
#include "vdl2_burst.hpp"
namespace orcsdr::vdl2 {
namespace {
// ICAO VDL2 (25,20) header check matrix, as represented in dumpvdl2
// decode.c. Values are protocol coefficients, not reused software logic.
constexpr uint32_t kParityMasks[5] = {
    0x01fff0u, 0x07e1fe8u, 0x18e61e4u, 0x1b6a662u, 0x0d3caa1u
};
uint8_t parity(uint32_t value) {
  value ^= value >> 16;
  value ^= value >> 8;
  value ^= value >> 4;
  return static_cast<uint8_t>((0x6996u >> (value & 0x0fu)) & 1u);
}
uint8_t syndrome(uint32_t word) {
  uint8_t result = 0;
  for (unsigned i = 0; i < 5; ++i)
    result |= static_cast<uint8_t>(parity(word & kParityMasks[i]) << (4u-i));
  return result;
}
uint32_t reverse17(uint32_t word) {
  uint32_t result = 0;
  for (unsigned i = 0; i < 17; ++i) { result = (result << 1u) | (word & 1u); word >>= 1u; }
  return result;
}
} // namespace

bool decode_burst_header(uint32_t header25, BurstHeader& out) {
  out = {};
  if (header25 & 0xfe000000u) return false; // more than 25 input bits
  if (header25 & 0x1c00000u) return false;  // three reserved symbol bits must be zero
  const uint8_t s = syndrome(header25);
  out.syndrome = s;
  if (s) {
    // Every 1-bit position in the 22 transmitted data/check bits has a
    // nonzero, unique syndrome under the standardized parity masks.
    int location = -1;
    for (unsigned i = 0; i < 22; ++i) {
      if (syndrome(uint32_t(1u) << i) == s) {
        if (location >= 0) return false;
        location = static_cast<int>(i);
      }
    }
    if (location < 0) return false;
    header25 ^= uint32_t(1u) << location;
    if (syndrome(header25)) return false;
    out.corrected_header_bits = 1;
  }
  const uint32_t transmitted_length_reversed = (header25 >> 5u) & 0x1ffffu;
  out.data_bits = reverse17(transmitted_length_reversed);
  return true;
}
uint8_t Descrambler15::apply_bit(uint8_t bit) {
  // Bit-level operation: feedback polynomial x^15+x+1. The feedback stream
  // is not data-dependent (same update for scrambling or descrambling).
  const uint8_t feedback = static_cast<uint8_t>((state_ ^ (state_ >> 14u)) & 1u);
  state_ = static_cast<uint16_t>((state_ >> 1u) | (uint16_t(feedback) << 14u));
  return static_cast<uint8_t>((bit & 1u) ^ feedback);
}
uint8_t final_block_rs_parity(size_t octets) {
  if (octets < 3u) return 0u;
  if (octets < 31u) return 2u;
  if (octets < 68u) return 4u;
  return 6u;
}
bool pack_lsb_first(const uint8_t* bits, size_t bit_count, uint8_t* bytes, size_t byte_capacity) {
  if ((!bits && bit_count) || (!bytes && bit_count) || (bit_count % 8u)) return false;
  if (bit_count / 8u > byte_capacity) return false;
  for (size_t i = 0; i < bit_count / 8u; ++i) {
    uint8_t value = 0;
    for (unsigned bit = 0; bit < 8u; ++bit)
      value |= static_cast<uint8_t>((bits[i*8u+bit] & 1u) << bit);
    bytes[i] = value;
  }
  return true;
}
} // namespace orcsdr::vdl2

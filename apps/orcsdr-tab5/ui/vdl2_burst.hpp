// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// VDL Mode 2 post-synchronization header/scrambler primitives.
// Reference: ETSI EN 301 841-1 §5.4 (3+17+5 bits), ICAO Annex 10;
// validated *reference conventions* from szpajder/dumpvdl2 decode.c/bitstream.c.
// These are NOT a physical D8PSK demodulator or an RS correction engine.
#include <cstddef>
#include <cstdint>
namespace orcsdr::vdl2 {
constexpr uint32_t kHeaderBits = 25u;   // 3 reserved + 17 transmission length + 5 header FEC
constexpr uint32_t kHeaderLengthBits = 17u;
constexpr uint32_t kHeaderCheckBits = 5u;
constexpr uint16_t kScramblerSeed = 0x6959u; // reference-confirmed: dumpvdl2 decode.c
struct BurstHeader {
  uint32_t data_bits = 0; // includes HDLC stuffed bits; excludes RS parity octets
  uint8_t corrected_header_bits = 0; // 0 or 1; cannot prove correction without subsequent CRC
  uint8_t syndrome = 0;
};
// Input is the 25 descrambled bits as an MSB-first word, with three reserved
// symbol bits in bits [24:22]. Returns false for invalid/unrepairable headers.
// Corrects at most one of the 22 non-reserved bits. A later RS/AVLC gate is mandatory.
bool decode_burst_header(uint32_t header25, BurstHeader& output);

// Bit sequence descrambler: x^15+x+1; initial register 0x6959.
// Applies one bit at a time; reset exactly at the header start and preserve
// state across header and payload, not at each incoming 8-bit octet.
class Descrambler15 {
 public:
  void reset() { state_ = kScramblerSeed; }
  uint8_t apply_bit(uint8_t bit);
  uint16_t state() const { return state_; }
 private:
  uint16_t state_ = kScramblerSeed;
};
// FEC octet count for a *final* (possibly shortened) RS(255,249) block.
// Reference-confirmed against dumpvdl2 decode.c, not a complete RS engine.
uint8_t final_block_rs_parity(size_t final_data_octets);
// Converts unpacked bits to RS/AVLC bytes, LSB-first HDLC wire order.
bool pack_lsb_first(const uint8_t* bits, size_t bit_count, uint8_t* bytes, size_t byte_capacity);
} // namespace orcsdr::vdl2

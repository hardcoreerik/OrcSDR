// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Independent, bounded RS(255,249) VDL2 error/erasure correction and
// column interleaving. Ref: ICAO Annex 10 Vol III; dumpvdl2/src/rs.c;
// cross-check: airframesio/xng crates/xng-mode-vdl2/src/interleave.rs.
#include <array>
#include <cstddef>
#include <cstdint>
namespace orcsdr::vdl2 {
constexpr size_t kRsData = 249;
constexpr size_t kRsBlock = 255;
constexpr size_t kRsCheck = 6;
constexpr size_t kMaxVdl2DataBits = 16000;  // conservative implementation bound
constexpr size_t kMaxRsRows = (kMaxVdl2DataBits + 1991u)/1992u;
constexpr size_t kMaxVdl2TransmittedBits = (kMaxVdl2DataBits + 7u)/8u*8u + kMaxRsRows*kRsCheck*8u;
struct FecResult { size_t data_bits = 0; uint8_t corrected_symbols = 0; };
class RsCodec {
public:
  RsCodec();
  // 249 data bytes, zero-padded after actual content. 6 parity bytes.
  void encode(const uint8_t data[kRsData], uint8_t parity[kRsCheck]) const;
  // 255 codeword bytes, with erasure positions for omitted parity bytes.
  // On failure, caller MUST discard the modified codeword.
  bool correct(uint8_t codeword[kRsBlock], const uint8_t* erasures,
               size_t erasure_count, size_t& corrected) const;
  // Bytes packed LSB-first; reads exactly the stated number of transmitted bits.
  // Returns TL-bits of post-FEC AVLC data, again packed LSB-first.
  bool deinterleave(const uint8_t* tx_bits, size_t tx_count, size_t data_bits,
                    uint8_t* data_out, size_t capacity, FecResult& result);
  // Test-harness frame builder: systematic RS encoding and column interleave.
  bool interleave(const uint8_t* data_bits_lsb, size_t data_bits,
                  uint8_t* tx_out, size_t capacity, size_t& tx_count);
  static size_t tx_bits_for(size_t data_bits);
private:
  uint8_t mul(uint8_t a, uint8_t b) const;
  uint8_t div(uint8_t a, uint8_t b) const;
  uint8_t apow(unsigned e) const { return exp_[e % 255u]; }
  std::array<uint8_t, 512> exp_{};
  std::array<uint8_t, 256> log_{};
  std::array<uint8_t, 7> generator_{};
  // Retained state (not temporary stack). No heap allocation in decoder.
  std::array<std::array<uint8_t, kRsBlock>, kMaxRsRows> grid_{};
};
}

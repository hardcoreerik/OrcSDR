#pragma once
// DMR voice de-interleave: 72 on-air bits -> 49-bit AMBE+2 parameter vector.
//
// The schedule below was derived from captured data against the reference vectors
// (see docs/dmr_voice_fec.md) rather than transcribed from any implementation, so
// nothing here is copied from GPL sources and no external table is used.
//
// On a 72-bit frame the bits form four residue classes mod 4:
//
//   payload[0..11]   = onair[4k]             k = 0..11    C0 data
//   payload[12..23]  = onair[21+4k] ^ ks[k]  k = 1..12    C1 data (whitened)
//   payload[24..30]  = onair[46+4k]          k = 0..6     C2 first seven
//   payload[31..48]  = onair[3+4k]           k = 0..17    C2 rest + C3
//
// The whitening keystream is seeded by C0's twelve data bits:
//   pr[0] = 16 * (c0 & 0x0FFF);  pr[i] = (173*pr[i-1] + 13849) & 0xFFFF
//   ks[i] = pr[i] >> 15
//
// mbelib consumes the 49-bit vector directly, so the Golay parity in the other
// bits of the 72 is needed only for error correction, not for decoding. That is
// why this file does not implement Golay at all.
#include <cstddef>
#include <cstdint>

namespace orc::zero::dv {

// Unpack 72 bits from 9 big-endian bytes, MSB first per byte.
inline void bits72_from_bytes(const std::uint8_t* in, std::uint8_t onair[72]) {
  for (int i = 0; i < 72; ++i) onair[i] = (in[i / 8] >> (7 - (i % 8))) & 1;
}

// Keystream bit `index` (1..23) from the 12-bit C0 data word.
inline std::uint8_t keystream_bit(const std::uint8_t payload[49], int index) {
  std::uint32_t value = 0;
  for (int i = 0; i < 12; ++i) value = (value << 1) | payload[i];
  std::uint32_t pr = 16u * (value & 0x0FFF);
  for (int i = 1; i <= index; ++i) pr = (173u * pr + 13849u) & 0xFFFFu;
  return std::uint8_t((pr >> 15) & 1u);
}

// The one function that matters: 72 on-air bits to the 49 the vocoder wants.
inline void extract_ambe(const std::uint8_t onair[72], std::uint8_t payload[49]) {
  for (int k = 0; k < 12; ++k) payload[k] = onair[4 * k];

  // C1 is whitened with a keystream the transmitter seeded from C0, so C0 must be
  // in place first. One uncorrected C0 bit would re-randomise all of C1.
  for (int k = 1; k <= 12; ++k)
    payload[11 + k] = onair[21 + 4 * k] ^ keystream_bit(payload, k);

  for (int k = 0; k < 7; ++k) payload[24 + k] = onair[46 + 4 * k];
  for (int k = 0; k < 18; ++k) payload[31 + k] = onair[3 + 4 * k];
}

inline void extract_ambe_bytes(const std::uint8_t frame[9], std::uint8_t payload[49]) {
  std::uint8_t onair[72];
  bits72_from_bytes(frame, onair);
  extract_ambe(onair, payload);
}


// ---------------------------------------------------------------------------
// Golay(23,12) correction for C0 and C1.
//
// Both codewords are systematic: twelve data bits then eleven parity bits, with
// generator polynomial 0xC75. The code corrects up to three bit errors, which is
// why C1 depends on a clean C0 - one uncorrected C0 bit re-randomises all twelve
// C1 bits through the whitening keystream.
//
// Positions were recovered by correlation (see docs/dmr_voice_fec.md), so the
// tables below are ours: C0's twelve data bits sit in residue class 0, its parity
// continues that class and then class 1; C1 occupies classes 1 and 2, whitened.
// ---------------------------------------------------------------------------
namespace detail {

inline std::uint32_t golay_syndrome(std::uint32_t word) {
  std::uint32_t rem = word & 0x7FFFFFu;
  for (int bit = 22; bit >= 11; --bit)
    if (rem & (1u << bit)) rem ^= 0xC75u << (bit - 11);
  return rem & 0x7FFu;
}

// Syndrome -> error pattern, built once for patterns of weight 1..3.
inline const std::int32_t* golay_table() {
  static std::int32_t table[2048];
  static bool ready = false;
  if (ready) return table;
  for (int i = 0; i < 2048; ++i) table[i] = -1;
  const auto add = [&](std::uint32_t pattern) {
    const std::uint32_t syndrome = golay_syndrome(pattern);
    if (table[syndrome] < 0) table[syndrome] = std::int32_t(pattern);
  };
  for (int a = 0; a < 23; ++a) {
    add(1u << a);
    for (int b = a + 1; b < 23; ++b) {
      add((1u << a) | (1u << b));
      for (int c = b + 1; c < 23; ++c) add((1u << a) | (1u << b) | (1u << c));
    }
  }
  ready = true;
  return table;
}

// Returns the number of bits corrected, or -1 when the syndrome matches no
// pattern of weight <= 3 (an uncorrectable word).
inline int golay_correct(std::uint32_t& word) {
  const std::uint32_t syndrome = golay_syndrome(word);
  if (syndrome == 0) return 0;
  const std::int32_t pattern = golay_table()[syndrome];
  if (pattern < 0) return -1;
  const std::uint32_t errors = std::uint32_t(pattern);
  word ^= errors;
  int count = 0;
  for (std::uint32_t bits = errors; bits; bits &= bits - 1) ++count;
  return count;
}

}  // namespace detail

// Positions of each codeword bit on the 72-bit frame.
inline int c0_position(int bit) {
  if (bit < 12) return 4 * bit;                       // 0..44
  if (bit < 18) return 48 + 4 * (bit - 12);           // 48..68
  return 1 + 4 * (bit - 18);                          // 1..17
}

inline int c1_position(int bit) {
  if (bit < 12) return 25 + 4 * bit;                  // 25..69
  return 2 + 4 * (bit - 12);                          // 2..42
}

// Full extraction with error correction. Reports how many bits were repaired
// (-1 for a codeword that could not be corrected, which is then used as received).
struct Corrections {
  int c0 = 0;
  int c1 = 0;
};

inline void extract_ambe_corrected(const std::uint8_t onair[72], std::uint8_t payload[49],
                                   Corrections* corrections = nullptr) {
  // C0 first: its corrected data seeds the keystream that C1 depends on.
  std::uint32_t c0 = 0;
  for (int bit = 0; bit < 23; ++bit)
    c0 = (c0 << 1) | onair[c0_position(bit)];
  // Rebuild in bit order with the LSB last, matching golay_syndrome's layout.
  std::uint32_t c0_word = 0;
  for (int bit = 0; bit < 23; ++bit)
    if (onair[c0_position(bit)]) c0_word |= (1u << (22 - bit));
  const int c0_fixed = detail::golay_correct(c0_word);
  if (corrections) corrections->c0 = c0_fixed;
  for (int bit = 0; bit < 12; ++bit) payload[bit] = (c0_word >> (22 - bit)) & 1u;

  std::uint32_t c1_word = 0;
  for (int bit = 0; bit < 23; ++bit) {
    const std::uint8_t raw = onair[c1_position(bit)];
    const std::uint8_t dewhitened = raw ^ keystream_bit(payload, bit + 1);
    if (dewhitened) c1_word |= (1u << (22 - bit));
  }
  const int c1_fixed = detail::golay_correct(c1_word);
  if (corrections) corrections->c1 = c1_fixed;
  for (int bit = 0; bit < 12; ++bit) payload[12 + bit] = (c1_word >> (22 - bit)) & 1u;

  for (int k = 0; k < 7; ++k) payload[24 + k] = onair[46 + 4 * k];
  for (int k = 0; k < 18; ++k) payload[31 + k] = onair[3 + 4 * k];
  (void)c0;
}

// Encoder, present so the correction path can be tested by round trip with
// injected errors rather than trusted on faith. Not used in the receive path.
inline std::uint32_t golay_encode12(std::uint32_t data12) {
  std::uint32_t work = (data12 & 0xFFFu) << 11;
  std::uint32_t rem = work;
  for (int bit = 22; bit >= 11; --bit)
    if (rem & (1u << bit)) rem ^= 0xC75u << (bit - 11);
  return ((data12 & 0xFFFu) << 11) | (rem & 0x7FFu);
}
}  // namespace orc::zero::dv

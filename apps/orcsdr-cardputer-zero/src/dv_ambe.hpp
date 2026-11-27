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

}  // namespace orc::zero::dv

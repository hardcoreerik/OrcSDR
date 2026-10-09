#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::codec {

// JS8 Normal physical/bit layer, from docs/js8/spec (js8_tone_map.json, js8_ldpc_174_87.json, js8_crc12.json, js8_message_formats.json).
//
//   79 channel tones = 3 sync blocks (0-6, 36-42, 72-78) + 58 data tones (7-35 and 43-71).
//   Each data tone carries 3 bits, MSB first, natural binary (tone index = bit value).
//   The 174 bits are the codeword in this order: 87 parity bits (data tones 7-35), then 87 information bits (data tones 43-71).
//   Information bits: 72 text bits (12 characters of 6 bits), 3 frame-flag bits, 12 CRC bits.
//
// Everything here is allocation-free and uses single-precision float only.
constexpr size_t kChannelTones = 79;
constexpr size_t kDataTones = 58;
constexpr size_t kCodewordBits = 174;
constexpr size_t kParityBits = 87;
constexpr size_t kInfoBits = 87;
constexpr size_t kTextBits = 72;
constexpr size_t kTextChars = 12;

// Where the data tones sit in the 79-tone frame.
inline constexpr size_t data_tone_position(size_t index) { return index < 29 ? 7 + index : 43 + (index - 29); }

// Per-bit log-likelihood ratios (positive = the bit is 0) from the 79x8 tone energies (linear power, as measured per symbol and tone).
// `gain` scales the amplitude-domain log-sum-exp; amplitudes are normalised by the symbol's mean amplitude, so the result does not depend on the
// absolute level. Out of range or non-finite energies give a zero LLR for that symbol's bits.
void tone_llrs(const float energy[kChannelTones][8], float gain, float llr[kCodewordBits]);

// Hard-decision bits from the tones' strongest-energy decisions (natural binary, codeword order).
void hard_bits_from_tones(const uint8_t tones[kChannelTones], uint8_t bits[kCodewordBits]);

// Number of the 87 parity checks the 0/1 codeword leaves unsatisfied.
uint16_t syndrome_weight(const uint8_t codeword[kCodewordBits]);

// CRC-12 (polynomial 0xC06, MSB first, not reflected, 75 bits followed by 13 zero bits, final XOR 0x2A) of the first 75 information bits.
uint16_t crc12(const uint8_t info75[75]);
// True when the CRC field (information bits 75..86) matches the CRC of the first 75 information bits.
bool crc_valid(const uint8_t codeword[kCodewordBits]);

// The 6-bit alphabet of the 12 text characters.
inline constexpr char kAlphabet[65] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+";

struct Fields {
  uint8_t kind = 0;                  // first three text bits (3 = directed)
  uint8_t flags = 0;                 // the three frame-flag bits after the text (3 = first and last frame of a message)
  char text[kTextChars + 1]{};       // the 12 raw characters, NUL terminated
  uint8_t payload[kTextBits]{};      // the 72 text bits, one per byte
};

// Splits a codeword (its information bits) into fields. Does not check the CRC.
void extract_fields(const uint8_t codeword[kCodewordBits], Fields* out);

bool self_check();

}  // namespace orcsdr::js8::codec

#include "js8_codec.hpp"

#include "js8_ldpc_graph.hpp"

#include <algorithm>
#include <cmath>

namespace orcsdr::js8::codec {

void tone_llrs(const float energy[kChannelTones][8], float gain, float llr[kCodewordBits]) {
  for (size_t s = 0; s < kDataTones; ++s) {
    const float* e = energy[data_tone_position(s)];
    float amp[8];
    float mean = 0.0f;
    bool ok = true;
    for (size_t t = 0; t < 8; ++t) {
      if (!(e[t] >= 0.0f) || !std::isfinite(e[t])) ok = false;
      amp[t] = ok ? std::sqrt(e[t]) : 0.0f;
      mean += amp[t];
    }
    mean *= 0.125f;
    for (size_t b = 0; b < 3; ++b) {
      float value = 0.0f;
      if (ok && mean > 0.0f) {
        const float inv = gain / mean;
        // log-sum-exp over the tones whose bit is 0 minus the same over the tones whose bit is 1, with the maximum factored out for stability
        float top = 0.0f;
        for (size_t t = 0; t < 8; ++t) top = std::max(top, amp[t] * inv);
        float zero = 0.0f, one = 0.0f;
        for (size_t t = 0; t < 8; ++t) {
          const float v = std::exp(amp[t] * inv - top);
          if (((t >> (2 - b)) & 1u) != 0) one += v;
          else zero += v;
        }
        value = std::log(zero + 1.0e-30f) - std::log(one + 1.0e-30f);
      }
      llr[3 * s + b] = value;
    }
  }
}

void hard_bits_from_tones(const uint8_t tones[kChannelTones], uint8_t bits[kCodewordBits]) {
  for (size_t s = 0; s < kDataTones; ++s) {
    const uint8_t t = tones[data_tone_position(s)] & 7u;
    bits[3 * s + 0] = static_cast<uint8_t>((t >> 2) & 1u);
    bits[3 * s + 1] = static_cast<uint8_t>((t >> 1) & 1u);
    bits[3 * s + 2] = static_cast<uint8_t>(t & 1u);
  }
}

uint16_t syndrome_weight(const uint8_t codeword[kCodewordBits]) {
  uint16_t bad = 0;
  for (size_t check = 0; check < ldpc_graph::kChecks; ++check) {
    uint8_t parity = 0;
    for (size_t e = ldpc_graph::kCheckOffsets[check]; e < ldpc_graph::kCheckOffsets[check + 1]; ++e) parity ^= codeword[ldpc_graph::kCheckVariables[e]];
    bad = static_cast<uint16_t>(bad + (parity & 1u));
  }
  return bad;
}

uint16_t crc12(const uint8_t info75[75]) {
  uint16_t remainder = 0;
  for (size_t i = 0; i < 88; ++i) {
    const uint16_t bit = i < 75 ? (info75[i] & 1u) : 0u;
    const uint16_t top = (remainder >> 11) & 1u;
    remainder = static_cast<uint16_t>(((remainder << 1) & 0xFFFu) | bit);
    if (top != 0) remainder ^= 0xC06u;
  }
  return static_cast<uint16_t>(remainder ^ 0x02Au);
}

bool crc_valid(const uint8_t codeword[kCodewordBits]) {
  const uint8_t* info = codeword + kParityBits;
  uint16_t field = 0;
  for (size_t i = 0; i < 12; ++i) field = static_cast<uint16_t>((field << 1) | (info[75 + i] & 1u));
  return crc12(info) == field;
}

void extract_fields(const uint8_t codeword[kCodewordBits], Fields* out) {
  const uint8_t* info = codeword + kParityBits;
  *out = Fields{};
  for (size_t i = 0; i < kTextBits; ++i) out->payload[i] = info[i] & 1u;
  for (size_t c = 0; c < kTextChars; ++c) {
    unsigned v = 0;
    for (size_t b = 0; b < 6; ++b) v = (v << 1) | (info[6 * c + b] & 1u);
    out->text[c] = kAlphabet[v];
  }
  out->text[kTextChars] = '\0';
  out->kind = static_cast<uint8_t>(((info[0] & 1u) << 2) | ((info[1] & 1u) << 1) | (info[2] & 1u));
  out->flags = static_cast<uint8_t>(((info[72] & 1u) << 2) | ((info[73] & 1u) << 1) | (info[74] & 1u));
}

bool self_check() {
  uint8_t zeros[75]{};
  return ldpc_graph::kChecks == 87 && ldpc_graph::kEdges == 3920 && crc12(zeros) == crc12(zeros) && kAlphabet[64] == '\0';
}

}  // namespace orcsdr::js8::codec

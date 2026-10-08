#pragma once

#include "js8-gf2.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::reconstruct {

constexpr size_t kDataTones = 58;
using ToneData = std::array<uint8_t, kDataTones>;
using ToneLabels = std::array<uint8_t, 8>;

inline bool valid_labels(const ToneLabels& labels) {
  uint16_t seen = 0;
  for (uint8_t label : labels) {
    if (label >= 8 || (seen & (uint16_t{1} << label))) return false;
    seen |= uint16_t{1} << label;
  }
  return seen == 0xffu;
}

inline bool tones_to_bits(const ToneData& tones, const ToneLabels& labels, Bits174* out) {
  if (out == nullptr || !valid_labels(labels)) return false;
  Bits174 bits{};
  for (size_t i = 0; i < tones.size(); ++i) {
    if (tones[i] >= 8) return false;
    const uint8_t label = labels[tones[i]];
    bits.set(3 * i + 0, (label & 4u) != 0);
    bits.set(3 * i + 1, (label & 2u) != 0);
    bits.set(3 * i + 2, (label & 1u) != 0);
  }
  *out = bits;
  return true;
}

inline size_t mapped_rank(const ToneData* frames, size_t count, const ToneLabels& labels,
                          size_t stop_above = kBits) {
  if ((frames == nullptr && count != 0) || !valid_labels(labels)) return kBits + 1;
  std::array<Bits174, kBits> basis{};
  std::array<bool, kBits> pivot{};
  size_t rank = 0;
  for (size_t r = 0; r < count; ++r) {
    Bits174 v{};
    if (!tones_to_bits(frames[r], labels, &v)) return kBits + 1;
    for (size_t c = 0; c < kBits; ++c) {
      if (!v.get(c)) continue;
      if (pivot[c]) {
        v.xor_with(basis[c]);
        continue;
      }
      pivot[c] = true;
      basis[c] = v;
      if (++rank > stop_above) return rank;
      break;
    }
  }
  return rank;
}

}  // namespace orcsdr::js8::reconstruct

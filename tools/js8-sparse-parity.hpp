#pragma once

#include "js8-gf2.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace orcsdr::js8::reconstruct {

struct Signature {
  std::array<uint64_t, kWords> words{};

  Signature& xor_with(const Signature& other) {
    for (size_t i = 0; i < words.size(); ++i) words[i] ^= other.words[i];
    return *this;
  }
};

inline Signature operator^(Signature a, const Signature& b) {
  return a.xor_with(b);
}

inline bool operator==(const Signature& a, const Signature& b) {
  return a.words == b.words;
}

inline bool operator<(const Signature& a, const Signature& b) {
  return a.words < b.words;
}

struct Triple {
  uint16_t a = 0;
  uint16_t b = 0;
  uint16_t c = 0;
};

inline bool disjoint(const Triple& x, const Triple& y) {
  return x.a != y.a && x.a != y.b && x.a != y.c &&
         x.b != y.a && x.b != y.b && x.b != y.c &&
         x.c != y.a && x.c != y.b && x.c != y.c;
}

struct TripleEntry {
  Signature signature{};
  Triple triple{};
};

inline size_t find_weight6(const Signature* columns, size_t column_count,
                           Bits174* output, size_t capacity) {
  if (columns == nullptr || output == nullptr || capacity == 0 ||
      column_count < 6 || column_count > kBits)
    return 0;

  const size_t triples =
      column_count * (column_count - 1) * (column_count - 2) / 6;
  std::vector<TripleEntry> entries;
  entries.reserve(triples);

  for (size_t a = 0; a + 2 < column_count; ++a)
    for (size_t b = a + 1; b + 1 < column_count; ++b)
      for (size_t c = b + 1; c < column_count; ++c)
        entries.push_back(TripleEntry{
            columns[a] ^ columns[b] ^ columns[c],
            Triple{static_cast<uint16_t>(a), static_cast<uint16_t>(b),
                   static_cast<uint16_t>(c)}});

  std::sort(entries.begin(), entries.end(),
            [](const TripleEntry& x, const TripleEntry& y) {
              return x.signature < y.signature;
            });

  size_t found = 0;
  size_t begin = 0;
  while (begin < entries.size() && found < capacity) {
    size_t end = begin + 1;
    while (end < entries.size() &&
           entries[end].signature == entries[begin].signature)
      ++end;

    for (size_t i = begin; i < end && found < capacity; ++i) {
      for (size_t j = i + 1; j < end && found < capacity; ++j) {
        const Triple& a = entries[i].triple;
        const Triple& b = entries[j].triple;
        if (!disjoint(a, b)) continue;

        Bits174 check{};
        check.set(a.a);
        check.set(a.b);
        check.set(a.c);
        check.set(b.a);
        check.set(b.b);
        check.set(b.c);

        bool duplicate = false;
        for (size_t k = 0; k < found; ++k) {
          if (output[k].words == check.words) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) output[found++] = check;
      }
    }
    begin = end;
  }

  return found;
}

}  // namespace orcsdr::js8::reconstruct

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::reconstruct {

constexpr size_t kBits = 174;
constexpr size_t kWords = (kBits + 63u) / 64u;

struct Bits174 {
  std::array<uint64_t, kWords> words{};

  bool get(size_t bit) const {
    return bit < kBits && ((words[bit / 64u] >> (bit % 64u)) & 1u) != 0;
  }
  void set(size_t bit, bool value = true) {
    if (bit >= kBits) return;
    const uint64_t mask = uint64_t{1} << (bit % 64u);
    if (value) words[bit / 64u] |= mask;
    else words[bit / 64u] &= ~mask;
  }
  void xor_with(const Bits174& other) {
    for (size_t i = 0; i < kWords; ++i) words[i] ^= other.words[i];
  }
  bool any() const {
    for (uint64_t w : words) if (w != 0) return true;
    return false;
  }
};

inline bool dot(const Bits174& a, const Bits174& b) {
  unsigned parity = 0;
  for (size_t i = 0; i < kWords; ++i)
    parity ^= static_cast<unsigned>(__builtin_popcountll(a.words[i] & b.words[i]) & 1u);
  return parity != 0;
}

struct CodeSpace {
  size_t rank = 0;
  size_t nullity = 0;
  std::array<Bits174, kBits> pivot_rows{};
  std::array<bool, kBits> has_pivot{};
  std::array<Bits174, kBits> parity_basis{};
};

inline bool parse_bits(const char* text, size_t length, Bits174* out) {
  if (text == nullptr || out == nullptr || length != kBits) return false;
  Bits174 v{};
  for (size_t i = 0; i < kBits; ++i) {
    if (text[i] == '1') v.set(i);
    else if (text[i] != '0') return false;
  }
  *out = v;
  return true;
}

inline bool analyze(const Bits174* rows, size_t count, CodeSpace* out) {
  if (out == nullptr || (rows == nullptr && count != 0)) return false;
  CodeSpace result{};

  for (size_t r = 0; r < count; ++r) {
    Bits174 v = rows[r];
    for (size_t c = 0; c < kBits; ++c) {
      if (!v.get(c)) continue;
      if (result.has_pivot[c]) {
        v.xor_with(result.pivot_rows[c]);
        continue;
      }
      result.has_pivot[c] = true;
      result.pivot_rows[c] = v;
      ++result.rank;
      break;
    }
  }

  for (size_t c = kBits; c-- > 0;) {
    if (!result.has_pivot[c]) continue;
    for (size_t earlier = 0; earlier < c; ++earlier) {
      if (result.has_pivot[earlier] && result.pivot_rows[earlier].get(c))
        result.pivot_rows[earlier].xor_with(result.pivot_rows[c]);
    }
  }

  size_t n = 0;
  for (size_t free_col = 0; free_col < kBits; ++free_col) {
    if (result.has_pivot[free_col]) continue;
    Bits174 h{};
    h.set(free_col);
    for (size_t c = 0; c < kBits; ++c) {
      if (!result.has_pivot[c]) continue;
      if (dot(result.pivot_rows[c], h)) h.set(c);
    }
    result.parity_basis[n++] = h;
  }
  result.nullity = n;
  if (result.rank + result.nullity != kBits) return false;
  *out = result;
  return true;
}

inline bool satisfies(const Bits174& word, const CodeSpace& space) {
  for (size_t i = 0; i < space.nullity; ++i)
    if (dot(word, space.parity_basis[i])) return false;
  return true;
}

}  // namespace orcsdr::js8::reconstruct

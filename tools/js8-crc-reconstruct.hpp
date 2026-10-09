#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::reconstruct {

constexpr size_t kInfoBits = 87;
constexpr size_t kPayloadBits = 75;
constexpr size_t kCrcBits = 12;

struct Info87 {
  std::array<uint8_t, kInfoBits> bits{};
};

inline bool parse_info87(const char* text, size_t length, Info87* out) {
  if (text == nullptr || out == nullptr || length != kInfoBits) return false;
  Info87 value{};
  for (size_t i = 0; i < kInfoBits; ++i) {
    if (text[i] != '0' && text[i] != '1') return false;
    value.bits[i] = static_cast<uint8_t>(text[i] - '0');
  }
  *out = value;
  return true;
}

inline uint16_t observed_crc(const Info87& word, bool reverse_bits) {
  uint16_t value = 0;
  for (size_t i = 0; i < kCrcBits; ++i) {
    const size_t index = reverse_bits ? (kCrcBits - 1u - i) : i;
    value = static_cast<uint16_t>(
        (value << 1u) | word.bits[kPayloadBits + index]);
  }
  return value;
}

inline uint16_t crc12_msb(const Info87& word, uint16_t poly) {
  uint16_t reg = 0;
  for (size_t i = 0; i < kPayloadBits; ++i) {
    const bool feedback = ((reg >> 11u) & 1u) != 0u;
    reg = static_cast<uint16_t>((reg << 1u) & 0x0fffu);
    if (feedback ^ (word.bits[i] != 0u)) reg ^= poly;
  }
  return static_cast<uint16_t>(reg & 0x0fffu);
}

inline uint16_t crc12_lsb(const Info87& word, uint16_t poly) {
  uint16_t reg = 0;
  for (size_t i = 0; i < kPayloadBits; ++i) {
    const bool feedback = (reg & 1u) != 0u;
    reg = static_cast<uint16_t>(reg >> 1u);
    if (feedback ^ (word.bits[i] != 0u)) reg ^= poly;
  }
  return static_cast<uint16_t>(reg & 0x0fffu);
}

inline uint16_t reverse12(uint16_t value) {
  uint16_t out = 0;
  for (size_t i = 0; i < 12; ++i) {
    out = static_cast<uint16_t>((out << 1u) | (value & 1u));
    value = static_cast<uint16_t>(value >> 1u);
  }
  return out;
}

}  // namespace orcsdr::js8::reconstruct

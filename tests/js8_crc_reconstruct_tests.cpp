#include "../tools/js8-crc-reconstruct.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using namespace orcsdr::js8::reconstruct;

namespace {

Info87 make_word(uint32_t seed, uint16_t poly, uint16_t offset) {
  Info87 word{};
  uint32_t state = seed;
  for (size_t i = 0; i < kPayloadBits; ++i) {
    state = state * 1664525u + 1013904223u;
    word.bits[i] = static_cast<uint8_t>((state >> 31u) & 1u);
  }

  const uint16_t check =
      static_cast<uint16_t>(crc12_msb(word, poly) ^ offset);
  for (size_t i = 0; i < kCrcBits; ++i) {
    word.bits[kPayloadBits + i] =
        static_cast<uint8_t>((check >> (11u - i)) & 1u);
  }
  return word;
}

}  // namespace

int main() {
  constexpr uint16_t kPoly = 0x80Fu;
  constexpr uint16_t kOffset = 0x5A3u;

  const Info87 a = make_word(1u, kPoly, kOffset);
  const Info87 b = make_word(2u, kPoly, kOffset);
  const Info87 c = make_word(3u, kPoly, kOffset);

  assert((crc12_msb(a, kPoly) ^ kOffset) == observed_crc(a, false));
  assert((crc12_msb(b, kPoly) ^ kOffset) == observed_crc(b, false));
  assert((crc12_msb(c, kPoly) ^ kOffset) == observed_crc(c, false));

  std::array<char, kInfoBits + 1> text{};
  for (size_t i = 0; i < kInfoBits; ++i)
    text[i] = a.bits[i] ? '1' : '0';

  Info87 parsed{};
  assert(parse_info87(text.data(), kInfoBits, &parsed));
  assert(parsed.bits == a.bits);

  text[20] = 'x';
  assert(!parse_info87(text.data(), kInfoBits, &parsed));
  assert(reverse12(reverse12(0xA53u)) == 0xA53u);

  std::puts("JS8 CRC reconstruction primitive tests: PASS");
  return 0;
}

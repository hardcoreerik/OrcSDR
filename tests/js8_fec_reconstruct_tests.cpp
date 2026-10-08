#include "../tools/js8-gf2.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <initializer_list>

using namespace orcsdr::js8::reconstruct;

namespace {
Bits174 make(std::initializer_list<size_t> bits) {
  Bits174 v{};
  for (size_t b : bits) v.set(b);
  return v;
}
}  // namespace

int main() {
  // A known four-dimensional subspace embedded in 174 bits:
  // x4=x0, x5=x1, x6=x2, x7=x3, with all remaining bits zero.
  const std::array<Bits174, 8> words{{
      make({0,4}), make({1,5}), make({2,6}), make({3,7}),
      make({0,1,4,5}), make({0,2,4,6}), make({1,3,5,7}), make({0,1,2,3,4,5,6,7})}};
  CodeSpace space{};
  assert(analyze(words.data(), words.size(), &space));
  assert(space.rank == 4);
  assert(space.nullity == 170);
  for (const auto& w : words) assert(satisfies(w, space));

  Bits174 invalid = words[0];
  invalid.set(10);
  assert(!satisfies(invalid, space));

  std::array<char, kBits + 1> text{};
  for (size_t i = 0; i < kBits; ++i) text[i] = '0';
  text[0] = text[4] = '1';
  Bits174 parsed{};
  assert(parse_bits(text.data(), kBits, &parsed));
  assert(parsed.words == words[0].words);
  text[9] = 'x';
  assert(!parse_bits(text.data(), kBits, &parsed));

  std::puts("JS8 GF(2) reconstruction tests: PASS");
  return 0;
}

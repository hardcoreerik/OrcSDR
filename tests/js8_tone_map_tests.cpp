#include "../tools/js8-tone-map.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using namespace orcsdr::js8::reconstruct;

int main() {
  const ToneLabels identity{{0,1,2,3,4,5,6,7}};
  const ToneLabels duplicate{{0,0,2,3,4,5,6,7}};
  assert(valid_labels(identity));
  assert(!valid_labels(duplicate));
  ToneLabels parsed{};
  assert(parse_labels("0,1,2,3,4,5,6,7", &parsed));
  assert(parsed == identity);
  assert(!parse_labels("0,1,2,3,4,5,6,6", &parsed));
  assert(!parse_labels("0,1,2,3,4,5,6", &parsed));

  ToneData tones{};
  tones.fill(0);
  tones[0] = 7;
  Bits174 bits{};
  assert(tones_to_bits(tones, identity, &bits));
  assert(bits.get(0) && bits.get(1) && bits.get(2));
  for (size_t i = 3; i < kBits; ++i) assert(!bits.get(i));

  std::array<ToneData, 3> frames{};
  frames[0].fill(0);
  frames[1].fill(0);
  frames[2].fill(0);
  frames[0][0] = 1;
  frames[1][1] = 2;
  frames[2][2] = 4;
  assert(mapped_rank(frames.data(), frames.size(), identity) == 3);
  assert(mapped_rank(frames.data(), frames.size(), duplicate) == kBits + 1);

  std::puts("JS8 tone-label reconstruction tests: PASS");
  return 0;
}

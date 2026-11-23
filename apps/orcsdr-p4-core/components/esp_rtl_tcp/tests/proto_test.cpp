// Host test of the rtl_tcp protocol core. Build and run:  g++ -std=c++17 -Wall -Wextra -I../include proto_test.cpp -o proto_test && ./proto_test
#include <cstdio>
#include <cstring>
#include <vector>
#include "rtl_tcp_proto.hpp"

using namespace esp_rtl_tcp::proto;
static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++fails; } } while (0)

int main() {
  uint8_t h[kHeaderBytes];
  encode_header(h, kTunerR828D, 29);
  const uint8_t want[12] = {'R', 'T', 'L', '0', 0, 0, 0, 6, 0, 0, 0, 29};
  CHECK(std::memcmp(h, want, 12) == 0);
  encode_header(h, kTunerR820T, 0x01020304);
  CHECK(h[4] == 0 && h[7] == 5 && h[8] == 1 && h[9] == 2 && h[10] == 3 && h[11] == 4);

  // Whole commands, then the same bytes split at every possible point.
  const uint8_t stream[] = {0x01, 0x05, 0xF5, 0xE1, 0x00,   // set_center 100,000,000 = 0x05F5E100
                            0x02, 0x00, 0x24, 0x9F, 0x00,   // set_rate 2,400,000 = 0x00249F00
                            0x04, 0xFF, 0xFF, 0xFF, 0xF6,   // gain -10 (signed tenth dB)
                            0x7F, 0x00, 0x00, 0x00, 0x01};  // unknown code
  for (size_t split = 0; split <= sizeof(stream); ++split) {
    CommandStream cs;
    std::vector<Command> got;
    cs.feed(stream, split, [&](const Command& c) { got.push_back(c); });
    cs.feed(stream + split, sizeof(stream) - split, [&](const Command& c) { got.push_back(c); });
    CHECK(got.size() == 4);
    CHECK(cs.pending() == 0);
    if (got.size() == 4) {
      CHECK(got[0].code == 0x01 && got[0].param == 0x05F5E100u);
      CHECK(got[1].code == 0x02 && got[1].param == 2400000u);
      CHECK(got[2].code == 0x04 && static_cast<int32_t>(got[2].param) == -10);
      CHECK(got[3].code == 0x7F && classify(got[3].code) == Op::unknown);
    }
  }
  // A partial trailing command stays pending and completes later.
  { CommandStream cs; int n = 0; cs.feed(stream, 7, [&](const Command&) { ++n; }); CHECK(n == 1 && cs.pending() == 2); cs.feed(stream + 7, 3, [&](const Command&) { ++n; }); CHECK(n == 2 && cs.pending() == 0); }

  CHECK(classify(0x01) == Op::set_center && classify(0x02) == Op::set_rate && classify(0x0D) == Op::gain_index && classify(0x0E) == Op::bias_tee);
  CHECK(classify(0x00) == Op::unknown && classify(0x0F) == Op::unknown && classify(0x40) == Op::unknown && classify(0x42) == Op::unknown && classify(0x56) == Op::unknown);
  CHECK(is_unsupported(classify(0x06)) && is_unsupported(classify(0x07)) && is_unsupported(classify(0x09)) && is_unsupported(classify(0x0A)) && is_unsupported(classify(0x0B)) && is_unsupported(classify(0x0C)));
  CHECK(!is_unsupported(classify(0x01)) && !is_unsupported(classify(0x7F)));

  std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails;
}

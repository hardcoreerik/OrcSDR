// Host tests for the hex helpers (hex_codec.cpp) used by the session, SD transfer, IQ transfer and LoRa commands.
// Expected behaviour is typed from the code this replaced in main.cpp (decode_hex, decode_hex_text,
// parse_hex_u32_exact, print_hex), including the forms it accepts that a strict parser would not.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/hex_codec_tests.cpp apps/orcsdr-tab5/ui/hex_codec.cpp
#include <cstdio>
#include <cstring>
#include <string>

#include "hex_codec.hpp"

namespace {
int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::string g_written;
int g_put_calls = 0;
void put(char digit) { g_written.push_back(digit); ++g_put_calls; }
}  // namespace

int main() {
  using namespace orcsdr::hex_codec;

  // decode: exact length, either case.
  uint8_t bytes[4] = {9, 9, 9, 9};
  CHECK(decode("00ff7FaB", bytes, 4) && bytes[0] == 0x00 && bytes[1] == 0xff && bytes[2] == 0x7f && bytes[3] == 0xab);
  CHECK(!decode("00ff7F", bytes, 4));        // too short
  CHECK(!decode("00ff7FaB00", bytes, 4));    // too long
  CHECK(!decode("00ff7Fa", bytes, 4));       // odd
  CHECK(!decode("00ff7Fag", bytes, 4));
  CHECK(!decode("00ff7F b", bytes, 4));
  CHECK(!decode("0x112233", bytes, 4));
  CHECK(decode("", bytes, 0));               // nothing asked, nothing given
  CHECK(!decode("", bytes, 1));
  // A bad digit late in the text leaves the earlier bytes written (callers discard the buffer on false).
  uint8_t partial[2] = {0x11, 0x22};
  CHECK(!decode("abzz", partial, 2) && partial[0] == 0xab && partial[1] == 0x22);
  // A 32-byte key, as PAIR sends it.
  uint8_t key[32];
  std::string key_text;
  for (int i = 0; i < 32; ++i) { char pair[3]; std::snprintf(pair, sizeof(pair), "%02x", i * 7 & 0xff); key_text += pair; }
  CHECK(decode(key_text.c_str(), key, sizeof(key)) && key[0] == 0 && key[1] == 7 && key[31] == (31 * 7 & 0xff));

  // decode_text: even length, must fit with its terminator.
  char text[5];
  CHECK(decode_text("4f726373", text, sizeof(text)) && std::strcmp(text, "Orcs") == 0);
  CHECK(!decode_text("4f72637364", text, sizeof(text)));   // 5 characters need 6 bytes
  CHECK(decode_text("", text, sizeof(text)) && text[0] == '\0');
  CHECK(!decode_text("4f7", text, sizeof(text)));
  CHECK(!decode_text("4g", text, sizeof(text)));
  CHECK(!decode_text("41", text, 1));                       // no room for the terminator
  CHECK(!decode_text("", text, 0));
  // An embedded NUL is accepted and cuts the C string short (existing behaviour).
  CHECK(decode_text("410042", text, sizeof(text)) && std::strcmp(text, "A") == 0 && text[2] == 'B');

  // parse_u32_exact: 8 characters that strtoul(16) takes whole.
  uint32_t value = 0;
  CHECK(parse_u32_exact("deadBEEF", &value) && value == 0xdeadbeefu);
  CHECK(parse_u32_exact("00000000", &value) && value == 0);
  CHECK(parse_u32_exact("ffffffff", &value) && value == 0xffffffffu);
  CHECK(!parse_u32_exact("deadbee", &value));
  CHECK(!parse_u32_exact("deadbeef0", &value));
  CHECK(!parse_u32_exact("deadbeeg", &value));
  CHECK(!parse_u32_exact("", &value));
  CHECK(!parse_u32_exact(nullptr, &value));
  CHECK(!parse_u32_exact("deadbeef", nullptr));
  // Existing behaviour, kept: strtoul also takes a 0x prefix, a sign and leading spaces inside the 8 characters.
  CHECK(parse_u32_exact("0x00beef", &value) && value == 0xbeefu);
  CHECK(parse_u32_exact("+0000012", &value) && value == 0x12u);
  CHECK(parse_u32_exact("  0000ff", &value) && value == 0xffu);
  CHECK(parse_u32_exact("-0000001", &value) && value == 0xffffffffu);

  // write: lower case, two put() calls per byte, nothing else.
  const uint8_t sample[] = {0x00, 0x0f, 0xa5, 0xff};
  write(sample, sizeof(sample), put);
  CHECK(g_written == "000fa5ff" && g_put_calls == 8);
  g_written.clear();
  g_put_calls = 0;
  write(sample, 0, put);
  CHECK(g_written.empty() && g_put_calls == 0);

  if (failures == 0) std::printf("hex_codec: all checks passed\n");
  return failures == 0 ? 0 : 1;
}

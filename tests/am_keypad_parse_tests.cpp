#include <cstdint>
#include <cstdio>

#include "am_frequency_parse.hpp"

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

}  // namespace

int main() {
  using orcsdr::am::parse_frequency_khz;

  uint32_t hz = 0;
  // Valid AM broadcast frequencies in kHz (520..1710).
  CHECK(parse_frequency_khz("520", &hz) && hz == 520000u);
  CHECK(parse_frequency_khz("1000", &hz) && hz == 1000000u);
  CHECK(parse_frequency_khz("1710", &hz) && hz == 1710000u);
  CHECK(parse_frequency_khz("530.0", &hz) && hz == 530000u);
  CHECK(parse_frequency_khz("1000.5", &hz) && hz == 1000500u);

  // Reject uint32_t wraparound (4295967 * 1000 wraps to 999704 Hz in uint32_t).
  hz = 12345u;
  CHECK(!parse_frequency_khz("4295967", &hz) && hz == 12345u);
  CHECK(!parse_frequency_khz("4294967", &hz));

  // Reject out-of-band, negative, non-finite, huge, empty, and trailing-junk strings.
  CHECK(!parse_frequency_khz("519", &hz));
  CHECK(!parse_frequency_khz("1711", &hz));
  CHECK(!parse_frequency_khz("-1000", &hz));
  CHECK(!parse_frequency_khz("inf", &hz));
  CHECK(!parse_frequency_khz("nan", &hz));
  CHECK(!parse_frequency_khz("1e30", &hz));
  CHECK(!parse_frequency_khz("", &hz));
  CHECK(!parse_frequency_khz(nullptr, &hz));
  CHECK(!parse_frequency_khz("1000abc", &hz));
  CHECK(!parse_frequency_khz(" 1000", &hz));
  CHECK(!parse_frequency_khz("0x3e8", &hz));

  if (failures == 0) std::printf("am_keypad_parse: all checks passed\n");
  return failures == 0 ? 0 : 1;
}

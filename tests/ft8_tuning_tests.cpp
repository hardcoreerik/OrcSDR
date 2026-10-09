// Expert tuning arithmetic: typed MHz parsing, key entry rules, step math with clamping, formatting.
#include "ft8_tuning.hpp"

#include <cstdio>
#include <cstring>

using namespace orcsdr::ft8::tuning;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static bool parse(const char* s, uint32_t* hz) { return parse_mhz(s, hz); }

int main() {
  CHECK(self_check());
  uint32_t hz = 0;
  CHECK(parse("7.078", &hz) && hz == 7078000u);
  CHECK(parse("14.0745", &hz) && hz == 14074500u);
  CHECK(parse("7", &hz) && hz == 7000000u);
  CHECK(parse("0.5", &hz) && hz == 500000u);
  CHECK(parse("7.047500", &hz) && hz == 7047500u);
  CHECK(parse("144.174", &hz) && hz == 144174000u);
  CHECK(parse("1766", &hz) && hz == 1766000000u);
  hz = 123;
  CHECK(!parse("", &hz) && hz == 123);              // a rejected entry never touches the output
  CHECK(!parse(".", &hz));
  CHECK(!parse("7.0.7", &hz));
  CHECK(!parse("7.1234567", &hz));                  // seventh decimal
  CHECK(!parse("7,078", &hz));
  CHECK(!parse("-7.0", &hz));
  CHECK(!parse("abc", &hz));
  CHECK(!parse("0.4", &hz));                        // below the absolute range
  CHECK(!parse("1767", &hz));                       // above the tuner range
  CHECK(!parse("99999999999999999999", &hz));       // no overflow
  CHECK(!parse(nullptr, &hz));
  CHECK(hz == 123);

  // Steps clamp at both ends and never wrap.
  CHECK(apply_steps(7078000u, 1, 1000u) == 7079000u);
  CHECK(apply_steps(7078000u, -3, 100u) == 7077700u);
  CHECK(apply_steps(7078000u, 10, 10u) == 7078100u);
  CHECK(apply_steps(kMinDialHz, -5, 100000u) == kMinDialHz);
  CHECK(apply_steps(kMaxDialHz, 5, 100000u) == kMaxDialHz);
  CHECK(apply_steps(7078000u, 2147483647, 100000u) == kMaxDialHz);    // detent count overflow-safe
  CHECK(apply_steps(7078000u, -2147483647, 100000u) == kMinDialHz);

  char text[24];
  format_mhz(7078000u, text, sizeof(text));
  CHECK(std::strcmp(text, "7.078000 MHz") == 0);
  format_mhz(144174000u, text, sizeof(text));
  CHECK(std::strcmp(text, "144.174000 MHz") == 0);
  char tiny[5];
  format_mhz(7078000u, tiny, sizeof(tiny));   // truncates safely
  CHECK(tiny[4] == '\0');

  CHECK(std::strcmp(step_label(0), "10 Hz") == 0 && std::strcmp(step_label(kDefaultStepIndex), "1 kHz") == 0 && std::strcmp(step_label(99), "?") == 0);

  // Key entry rules.
  char entry[12] = "";
  CHECK(entry_key(entry, sizeof(entry), '7') && std::strcmp(entry, "7") == 0);
  CHECK(entry_key(entry, sizeof(entry), '.') && std::strcmp(entry, "7.") == 0);
  CHECK(!entry_key(entry, sizeof(entry), '.'));                       // one dot only
  CHECK(entry_key(entry, sizeof(entry), '0') && entry_key(entry, sizeof(entry), '7') && entry_key(entry, sizeof(entry), '8'));
  CHECK(std::strcmp(entry, "7.078") == 0);
  CHECK(parse(entry, &hz) && hz == 7078000u);
  CHECK(entry_key(entry, sizeof(entry), '<') && std::strcmp(entry, "7.07") == 0);
  char zero[12] = "";
  CHECK(entry_key(zero, sizeof(zero), '0') && entry_key(zero, sizeof(zero), '7') && std::strcmp(zero, "7") == 0);   // "07" collapses
  char lead[12] = "";
  CHECK(entry_key(lead, sizeof(lead), '.') && std::strcmp(lead, "0.") == 0);                                     // ".5" -> "0.5"
  CHECK(entry_key(lead, sizeof(lead), '5') && parse(lead, &hz) && hz == 500000u);
  char decimals[16] = "7.";
  for (int i = 0; i < 6; ++i) CHECK(entry_key(decimals, sizeof(decimals), '1'));
  CHECK(!entry_key(decimals, sizeof(decimals), '1'));                  // seventh decimal refused
  char full[4] = "";
  CHECK(entry_key(full, sizeof(full), '1') && entry_key(full, sizeof(full), '2') && entry_key(full, sizeof(full), '3'));
  CHECK(!entry_key(full, sizeof(full), '4'));                          // capacity respected
  char empty[8] = "";
  CHECK(!entry_key(empty, sizeof(empty), '<'));
  CHECK(!entry_key(empty, sizeof(empty), 'x'));

  if (failures == 0) std::printf("ft8_tuning_tests OK\n");
  return failures == 0 ? 0 : 1;
}

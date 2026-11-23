// Host tests for the SD path rules (sd_paths.cpp) that guard SD_GET / SD_PUT / SD_REMOVE and the RDS replay file.
// Expected behaviour is typed from the code this replaced in main.cpp.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/sd_paths_tests.cpp apps/orcsdr-tab5/ui/sd_paths.cpp
#include <cstdio>
#include <string>

#include "sd_paths.hpp"

namespace {
int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)
}  // namespace

int main() {
  using orcsdr::sd_paths::is_acars_log;
  using orcsdr::sd_paths::remove_allowed;
  using orcsdr::sd_paths::transfer_allowed;

  CHECK(transfer_allowed("/orcsdr/a"));
  CHECK(transfer_allowed("/orcsdr/bands/fm.cfg"));
  CHECK(transfer_allowed("/orcsdr/acars/log 2026.csv"));   // spaces are printable
  CHECK(transfer_allowed("/orcsdr/~x"));                    // 0x7e is the last allowed byte

  CHECK(!transfer_allowed(nullptr));
  CHECK(!transfer_allowed(""));
  CHECK(!transfer_allowed("/orcsdr/"));                     // a name is required
  CHECK(!transfer_allowed("/orcsdr"));
  CHECK(!transfer_allowed("orcsdr/a"));
  CHECK(!transfer_allowed("/ORCSDR/a"));
  CHECK(!transfer_allowed("/other/a"));
  CHECK(!transfer_allowed("/orcsdr/../boot.bin"));
  CHECK(!transfer_allowed("/orcsdr/a..b"));                 // any ".." is refused
  CHECK(!transfer_allowed("/orcsdr/a\\b"));
  CHECK(!transfer_allowed("/orcsdr/a\"b"));
  CHECK(!transfer_allowed("/orcsdr/a\tb"));
  CHECK(!transfer_allowed("/orcsdr/a\x7f"));
  CHECK(!transfer_allowed("/orcsdr/\xc3\xa9"));             // non-ASCII

  // Length: shorter than kMaxPath (128) including the terminator.
  const std::string longest = "/orcsdr/" + std::string(orcsdr::sd_paths::kMaxPath - 1 - 8, 'a');
  CHECK(longest.size() == 127 && transfer_allowed(longest.c_str()));
  CHECK(!transfer_allowed((longest + "a").c_str()));

  CHECK(remove_allowed("/orcsdr/a"));
  CHECK(remove_allowed("/OrcSDR_Splash_1280x720_60fps_10s.orsplash"));
  CHECK(!remove_allowed(nullptr));
  CHECK(!remove_allowed("/orcsdr/a\"b"));
  CHECK(!remove_allowed("/OrcSDR_Splash_1280x720_60fps_10s.orsplash.bak"));
  CHECK(!remove_allowed("/orcsdr/"));
  CHECK(!remove_allowed("/other"));

  CHECK(is_acars_log("/orcsdr/acars/20261122.jsonl"));
  CHECK(is_acars_log("/orcsdr/acars/undated.jsonl"));
  CHECK(!is_acars_log(nullptr));
  CHECK(!is_acars_log("/orcsdr/acars/"));
  CHECK(!is_acars_log("/orcsdr/acars/other.bin"));
  CHECK(!is_acars_log("/orcsdr/acars/2026112.jsonl"));
  CHECK(!is_acars_log("/orcsdr/acars/202611220.jsonl"));
  CHECK(!is_acars_log("/orcsdr/acars/2026112a.jsonl"));
  CHECK(!is_acars_log("/orcsdr/acars/sub/20261122.jsonl"));
  CHECK(!is_acars_log("/orcsdr/acars/20261122.jsonl.bak"));

  if (failures == 0) std::printf("sd_paths: all checks passed\n");
  return failures == 0 ? 0 : 1;
}

#include "shortwave_hunt.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) \
  do { if (!(expression)) fail(#expression, __LINE__); } while (false)
}  // namespace

int main() {
  using namespace orcsdr;
  scan::Engine engine;
  shortwave::Hunt hunt;
  const auto* band = shortwave::band_for(9800000);
  CHECK(band != nullptr);
  CHECK(hunt.start(engine, *band, 9800000, 100));
  const auto progress = engine.progress();
  CHECK(progress.active);
  CHECK(progress.frequency_hz >= band->min_hz);
  CHECK(progress.frequency_hz <= band->max_hz);

  hunt.observe(9805000, -50.0f);
  hunt.observe(9795000, -35.0f);
  hunt.observe(9810000, -35.0f);
  CHECK(hunt.candidate_count() == 3);
  CHECK(hunt.candidate(0)->frequency_hz == 9795000);
  CHECK(hunt.candidate(1)->frequency_hz == 9810000);
  CHECK(hunt.candidate(2)->level_dbfs == -50.0f);
  CHECK(hunt.restore_frequency_hz() == 9800000);

  std::puts("shortwave_hunt_tests: PASS");
  return 0;
}

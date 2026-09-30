#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "filter_standards.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

using namespace orcsdr::filter_standards;

void test_standards_match_dashboard_defaults() {
  // These must stay equal to the defaults the dashboards already start with.
  CHECK(standards(Kind::wfm).standard_hz == 260000);
  CHECK(standards(Kind::nfm).standard_hz == 25000);
  CHECK(standards(Kind::am_broadcast).standard_hz == 10000);
  CHECK(standards(Kind::am_shortwave).standard_hz == 6000);
  CHECK(standards(Kind::cb_am).standard_hz == 10000);
  CHECK(standards(Kind::cb_ssb).standard_hz == 3000);
  CHECK(standards(Kind::airband_am).standard_hz == 10000);
}

void test_every_kind_is_well_formed() {
  for (int k = 0; k <= static_cast<int>(Kind::airband_am); ++k) {
    const Standards& s = standards(static_cast<Kind>(k));
    CHECK(s.name != nullptr && s.name[0] != '\0');
    CHECK(s.count <= kMaxPresets);
    if (s.count == 0) { CHECK(s.standard_hz == 0); continue; }
    bool has_standard = false;
    for (uint8_t i = 0; i < s.count; ++i) {
      CHECK(s.presets_hz[i] >= 2000 && s.presets_hz[i] <= 300000);
      CHECK(i == 0 || s.presets_hz[i] > s.presets_hz[i - 1]);   // ascending, no duplicates
      if (s.presets_hz[i] == s.standard_hz) has_standard = true;
    }
    CHECK(has_standard);
  }
}

void test_fixed_kind_has_no_presets() {
  CHECK(standards(Kind::fixed).count == 0);
  CHECK(nearest_preset_hz(Kind::fixed, 12345) == 0);
}

void test_nearest_preset() {
  CHECK(nearest_preset_hz(Kind::wfm, 255000) == 260000);
  CHECK(nearest_preset_hz(Kind::wfm, 190000) == 200000);
  CHECK(nearest_preset_hz(Kind::wfm, 1000) == 150000);           // below the table
  CHECK(nearest_preset_hz(Kind::wfm, 900000) == 300000);         // above the table
  CHECK(nearest_preset_hz(Kind::am_broadcast, 0) == 3000);
  CHECK(nearest_preset_hz(Kind::cb_ssb, 2600) == 2400);
  CHECK(nearest_preset_hz(Kind::cb_ssb, 2800) == 3000);
  CHECK(nearest_preset_hz(Kind::nfm, 25000) == 25000);
}

void test_out_of_range_kind_falls_back() {
  const Standards& s = standards(static_cast<Kind>(200));
  CHECK(s.count == 0);
}

}  // namespace

int main() {
  test_standards_match_dashboard_defaults();
  test_every_kind_is_well_formed();
  test_fixed_kind_has_no_presets();
  test_nearest_preset();
  test_out_of_range_kind_falls_back();
  CHECK(orcsdr::filter_standards::self_check());
  std::puts("filter_standards_tests: PASS");
  return 0;
}

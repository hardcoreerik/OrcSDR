#include "ft8_adif.hpp"
#include "ft8_conditions.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace orcsdr::ft8;

namespace {

void test_conditions_summary() {
  assert(conditions_self_check());
  Conditions c{};
  const GeoPoint station{44.0f, -123.0f, 0};
  assert(compute_conditions(nullptr, 0, station, &c) && c.stations == 0 && !c.farthest_valid);
  assert(!compute_conditions(nullptr, 3, station, &c));
  assert(!compute_conditions(nullptr, 0, station, nullptr));
}

void test_median_and_sectors() {
  Decode d[3]{};
  const char* calls[] = {"A1AAA", "B2BBB", "C3CCC"};
  const char* grids[] = {"FN42", "DM03", "BP51"};  // east, south, north-west (Alaska)
  for (int i = 0; i < 3; ++i) {
    std::snprintf(d[i].callsign, sizeof(d[i].callsign), "%s", calls[i]);
    std::snprintf(d[i].grid, sizeof(d[i].grid), "%s", grids[i]);
    d[i].utc_epoch = 100 + i;
  }
  Conditions c{};
  assert(compute_conditions(d, 3, GeoPoint{44.0f, -123.0f, 0}, &c));
  assert(c.stations == 3);
  size_t total = 0;
  for (size_t s = 0; s < kBearingSectors; ++s) total += c.sector_counts[s];
  assert(total == 3);
  assert(c.median_km > 0.0f && c.median_km <= c.farthest_km);
}

void test_adif_records() {
  assert(adif_self_check());
  char header[256];
  const size_t h = adif_header(header, sizeof(header));
  assert(h > 0 && std::strstr(header, "<EOH>") != nullptr);
  assert(adif_header(header, 8) == 0);

  Decode d{};
  std::snprintf(d.callsign, sizeof(d.callsign), "N6ACA");
  d.utc_epoch = 1791440160u;
  d.audio_hz = 2400;
  char record[320];
  // No grid: the GRIDSQUARE field is omitted, not guessed.
  size_t n = adif_heard_record(d, 14074000u, record, sizeof(record));
  assert(n > 0 && std::strstr(record, "<BAND:3>20m") != nullptr && std::strstr(record, "GRIDSQUARE") == nullptr);
  assert(std::strstr(record, "Heard only") != nullptr);
  // FT4 is MFSK with a submode; JS8 is deliberately not written.
  d.mode = DigitalMode::ft4;
  n = adif_heard_record(d, 14080000u, record, sizeof(record));
  assert(n > 0 && std::strstr(record, "<MODE:4>MFSK") != nullptr && std::strstr(record, "<SUBMODE:3>FT4") != nullptr);
  d.mode = DigitalMode::js8_normal;
  assert(adif_heard_record(d, 7078000u, record, sizeof(record)) == 0);
  d.mode = DigitalMode::ft8;
  d.callsign[0] = '\0';
  assert(adif_heard_record(d, 7074000u, record, sizeof(record)) == 0);
  // Epoch 0 is a valid date, and a leap day formats correctly (2024-02-29 12:00:00 UTC = 1709208000).
  std::snprintf(d.callsign, sizeof(d.callsign), "K1ABC");
  d.utc_epoch = 1709208000u;
  assert(adif_heard_record(d, 7074000u, record, sizeof(record)) > 0 &&
         std::strstr(record, "<QSO_DATE:8>20240229") != nullptr && std::strstr(record, "<TIME_ON:6>120000") != nullptr);
}

}  // namespace

int main() {
  test_conditions_summary();
  test_median_and_sectors();
  test_adif_records();
  std::puts("ft8_conditions_tests: PASS");
  return 0;
}

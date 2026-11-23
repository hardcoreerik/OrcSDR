// Verifies the station index reader against the real FCC-derived table.
//
// The index itself is generated (tools/build_station_index.py) and not committed
// at 1.2 MB, so this test skips cleanly when the file is absent rather than
// failing on a checkout that never built it.
#include <cstdio>
#include <string>

#include "station_directory.hpp"

namespace st = orc::zero::stations;

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "fm_am_stations.idx";
  st::Directory directory;
  std::string error;
  if (!directory.load(path, error)) {
    std::printf("[SKIP] %s\n", error.c_str());
    return 0;
  }

  int failures = 0;
  const auto check = [&](bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
  };

  std::printf("records %zu\n", directory.size());
  check(directory.size() == 26443, "record count matches the FCC snapshot (26,443)");
  check(!directory.provenance().empty(), "provenance is recorded in the header");
  std::printf("       provenance: %s\n", directory.provenance().c_str());

  // Sorted by frequency, so the first record is the lowest AM dial position.
  const auto& first = directory.at(0);
  std::printf("       first %s (%s) %u Hz %s %s\n", first.call,
              first.service == st::kServiceAm ? "AM" : "FM", first.frequency_hz, first.city,
              first.region);
  check(first.service == st::kServiceAm, "lowest entry is an AM station");
  bool sorted = true;
  for (size_t n = 1; n < directory.size(); ++n)
    if (directory.at(n).frequency_hz < directory.at(n - 1).frequency_hz) sorted = false;
  check(sorted, "table is sorted by frequency");

  // A specific record from the source file: WAUK on 540 kHz.
  const size_t wauk = directory.find_call("WAUK");
  check(wauk < directory.size(), "WAUK is present");
  if (wauk < directory.size()) {
    const auto& record = directory.at(wauk);
    std::printf("       WAUK %u Hz %s %s\n", record.frequency_hz, record.city, record.region);
    check(record.frequency_hz == 540000, "WAUK is on 540 kHz");
    check(std::string(record.city) == "JACKSON", "WAUK city decoded");
    check(std::string(record.region) == "WI", "WAUK region decoded");
  }

  // Tuning-order lookup: everything within 200 kHz of 90.0 MHz is FM.
  const auto nearby = directory.near_frequency(90000000, 200000, st::kServiceFm);
  std::printf("       %zu FM stations within 200 kHz of 90.0 MHz\n", nearby.size());
  check(!nearby.empty(), "frequency window returns stations");
  bool all_fm = true;
  for (size_t index : nearby)
    if (directory.at(index).service != st::kServiceFm) all_fm = false;
  check(all_fm, "frequency window honours the service filter");

  // Nearest to the device's region (Eugene, Oregon): should return real FM
  // stations within a few tens of kilometres.
  const auto local = directory.nearest(44.05, -123.09, st::kServiceFm, 5);
  check(local.size() == 5, "nearest returns the requested count");
  for (size_t index : local) {
    const auto& record = directory.at(index);
    std::printf("       near %s %u Hz %s %s (%.4f, %.4f)\n", record.call, record.frequency_hz,
                record.city, record.region, record.latitude(), record.longitude());
  }
  if (!local.empty()) {
    const auto& record = directory.at(local.front());
    const double dx = (record.longitude() + 123.09) * 111.32 * std::cos(44.05 * 3.14159 / 180.0);
    const double dy = (record.latitude() - 44.05) * 111.32;
    const double km = std::sqrt(dx * dx + dy * dy);
    std::printf("       closest is %.1f km away\n", km);
    check(km < 100.0, "closest station is within 100 km of Eugene");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

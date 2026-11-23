// Reproduces exactly what the FM STATIONS page does, so the page and the test
// cannot disagree silently: same candidate paths, same city table entry, same
// nearest() call, same distance maths.
#include <cmath>
#include <cstdio>
#include <string>

#include "compact/control_model.hpp"
#include "station_directory.hpp"

namespace st = orc::zero::stations;

int main() {
  const char* candidates[] = {"fm_am_stations.idx",
                              "apps/orcsdr-cardputer-zero/fm_am_stations.idx",
                              "data/fm_am_stations.idx"};
  st::Directory db;
  std::string error;
  const char* used = "none";
  for (const char* path : candidates)
    if (db.load(path, error)) {
      used = path;
      break;
    }
  std::printf("loaded %s (%zu records) %s\n", used, db.size(), db.empty() ? error.c_str() : "");

  const auto& city = orc::zero::kFmCities[0];
  const double lat = city.latitude_e7 / 1e7, lon = city.longitude_e7 / 1e7;
  std::printf("city %s lat=%.6f lon=%.6f\n", city.label, lat, lon);

  const auto found = db.nearest(lat, lon, st::kServiceFm, 7);
  for (size_t index : found) {
    const auto& r = db.at(index);
    const double dx = (r.longitude() - lon) * 111.32 * std::cos(lat * 3.14159265358979323846 / 180.0);
    const double dy = (r.latitude() - lat) * 111.32;
    std::printf("  %-8s %u Hz %-12s %-3s lat=%.4f lon=%.4f  %.1f km\n", r.call, r.frequency_hz,
                r.city, r.region, r.latitude(), r.longitude(), std::sqrt(dx * dx + dy * dy));
  }
  return 0;
}

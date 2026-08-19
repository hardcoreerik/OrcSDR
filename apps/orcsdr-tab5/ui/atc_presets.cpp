#include "atc_presets.hpp"

#include <Arduino.h>

#include <cmath>
#include <cstring>

namespace orcsdr::atc {
namespace {

constexpr size_t kCapacity = 24;
Preset g_presets[kCapacity]{};
size_t g_count = 0;

bool valid(const Preset& preset) {
  return preset.latitude_e7 >= -900000000 && preset.latitude_e7 <= 900000000 &&
         preset.longitude_e7 >= -1800000000 && preset.longitude_e7 <= 1800000000 &&
         preset.frequency_hz >= 118000000 && preset.frequency_hz <= 137000000 && preset.label[0];
}

}  // namespace

bool load(fs::FS* filesystem) {
  g_count = 0;
  if (!filesystem) return false;
  File file = filesystem->open(kRuntimePath, FILE_READ);
  if (!file) return false;
  char line[112]{};
  const size_t header_size = file.readBytesUntil('\n', line, sizeof(line) - 1);
  line[header_size] = '\0';
  if (strcmp(line, "ORCCAT1") != 0) { file.close(); return false; }
  while (file.available() && g_count < kCapacity) {
    const size_t size = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[size] = '\0';
    Preset preset{};
    if (sscanf(line, "ATC %ld %ld %lu %31[^\n]", &preset.latitude_e7,
               &preset.longitude_e7, &preset.frequency_hz, preset.label) == 4 && valid(preset))
      g_presets[g_count++] = preset;
  }
  file.close();
  return g_count != 0;
}

bool nearest(int32_t latitude_e7, int32_t longitude_e7, Preset* output) {
  if (!output || g_count == 0) return false;
  size_t best = 0;
  double best_distance = INFINITY;
  for (size_t i = 0; i < g_count; ++i) {
    const double lat = static_cast<double>(g_presets[i].latitude_e7) - latitude_e7;
    const double lon = static_cast<double>(g_presets[i].longitude_e7) - longitude_e7;
    const double distance = lat * lat + lon * lon;
    if (distance < best_distance) { best_distance = distance; best = i; }
  }
  *output = g_presets[best];
  return true;
}

bool self_check() {
  Preset sample{440000000, -1230000000, 118900000, "TEST"};
  return valid(sample) && !valid({0, 0, 117000000, "BAD"});
}

}  // namespace orcsdr::atc

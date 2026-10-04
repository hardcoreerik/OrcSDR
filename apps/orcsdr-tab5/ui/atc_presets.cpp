#include "atc_presets.hpp"

#include "airband_catalog.hpp"

#include <esp_attr.h>

#include <cstring>

namespace orcsdr::atc {
namespace {

orcsdr::storage::FileSystem* g_filesystem = nullptr;

// One answer is cached per receiver position. Finding the nearest record streams the whole
// aviation pack from the SD card, so it must not run again on every ADS-B screen refresh.
struct Cache {
  bool valid = false;
  bool found = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  Preset preset{};
};
Cache g_cache;

bool valid(const Preset& preset) {
  return preset.latitude_e7 >= -900000000 && preset.latitude_e7 <= 900000000 &&
         preset.longitude_e7 >= -1800000000 && preset.longitude_e7 <= 1800000000 &&
         orcsdr::airband::in_band(preset.frequency_hz) && preset.label[0];
}

}  // namespace

bool load(orcsdr::storage::FileSystem* filesystem) {
  g_filesystem = filesystem;
  g_cache = Cache{};
  if (!filesystem) return false;
  return filesystem->exists(kRuntimePath) || filesystem->exists(kLegacyRuntimePath);
}

bool nearest(int32_t latitude_e7, int32_t longitude_e7, Preset* output) {
  if (!output || !g_filesystem) return false;
  if (g_cache.valid && g_cache.latitude_e7 == latitude_e7 &&
      g_cache.longitude_e7 == longitude_e7) {
    if (!g_cache.found) return false;
    *output = g_cache.preset;
    return true;
  }

  // The catalog keeps only the nearest records, streams the file in chunks, and rejects far
  // rows on latitude alone, so this is bounded in memory and time whatever the pack size.
  EXT_RAM_BSS_ATTR static orcsdr::airband::Catalog catalog;
  orcsdr::airband::Location location{true, latitude_e7, longitude_e7};
  location.radius_nm = 0;
  g_cache = Cache{};
  g_cache.valid = true;
  g_cache.latitude_e7 = latitude_e7;
  g_cache.longitude_e7 = longitude_e7;
  if (!catalog.load(g_filesystem, location) || catalog.count() == 0) return false;

  const orcsdr::airband::CatalogEntry* best = catalog.entry(0);
  Preset preset{};
  preset.latitude_e7 = best->latitude_e7;
  preset.longitude_e7 = best->longitude_e7;
  preset.frequency_hz = best->frequency_hz;
  std::strncpy(preset.label, best->label, sizeof(preset.label) - 1);
  if (!valid(preset)) return false;
  g_cache.found = true;
  g_cache.preset = preset;
  *output = preset;
  return true;
}

bool self_check() {
  Preset sample{440000000, -1230000000, 118900000, "TEST"};
  return valid(sample) && !valid({0, 0, 117000000, "BAD"}) &&
         !valid({0, 0, 137000000, "BAD"});
}

}  // namespace orcsdr::atc

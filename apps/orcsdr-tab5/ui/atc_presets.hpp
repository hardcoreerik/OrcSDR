#pragma once

#include <cstdint>

#include "orcsdr_storage.hpp"

namespace orcsdr::atc {

constexpr const char kRuntimePath[] = "/orcsdr/data/faa_aviation.idx";

struct Preset {
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint32_t frequency_hz = 0;
  char label[32]{};
};

// FAA aviation indexes may optionally contain: ATC <lat_e7> <lon_e7> <hz> <label>.
bool load(orcsdr::storage::FileSystem* filesystem);
bool nearest(int32_t latitude_e7, int32_t longitude_e7, Preset* output);
bool self_check();

}  // namespace orcsdr::atc

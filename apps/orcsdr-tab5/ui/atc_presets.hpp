#pragma once

#include <cstdint>

#include "orcsdr_storage.hpp"

namespace orcsdr::atc {

constexpr const char kRuntimePath[] = "/orcsdr/data/aviation.idx";
constexpr const char kLegacyRuntimePath[] = "/orcsdr/data/faa_aviation.idx";

struct Preset {
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint32_t frequency_hz = 0;
  char label[32]{};
};

// Validate and remember the available aviation runtime pack. Both ORCAIR2 and
// legacy ORCCAT1 are accepted.
bool load(orcsdr::storage::FileSystem* filesystem);

// Stream the active pack and return the geographically nearest communication
// record. This avoids assuming that the first rows of a national/global pack
// are local to the receiver.
bool nearest(int32_t latitude_e7, int32_t longitude_e7, Preset* output);
bool self_check();

}  // namespace orcsdr::atc

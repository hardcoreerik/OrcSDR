#include "map_sources.hpp"

#if ORCSDR_HAS_EMBEDDED_WORLD_MAP
extern const uint8_t orcmaps_world_pmtiles_start[] asm("_binary_orcmaps_world_pmtiles_start");
extern const uint8_t orcmaps_world_pmtiles_end[] asm("_binary_orcmaps_world_pmtiles_end");
#endif

namespace orcsdr::map_sources {

const uint8_t* embedded_world(size_t* size) {
#if ORCSDR_HAS_EMBEDDED_WORLD_MAP
  if (size != nullptr) *size = static_cast<size_t>(orcmaps_world_pmtiles_end - orcmaps_world_pmtiles_start);
  return orcmaps_world_pmtiles_start;
#else
  if (size != nullptr) *size = 0;
  return nullptr;
#endif
}

}  // namespace orcsdr::map_sources

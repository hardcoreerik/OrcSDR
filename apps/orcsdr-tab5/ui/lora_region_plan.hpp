#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::lora_plan {

struct Region { const char* code; uint32_t start_hz; uint32_t end_hz; };

inline constexpr uint32_t kBandwidthHz = 250000;
inline constexpr std::array<Region,24> kRegions{{
  {"US",902000000,928000000},{"EU_433",433000000,434000000},
  {"EU_868",869400000,869650000},{"CN",470000000,510000000},
  {"JP",920500000,923500000},{"ANZ",915000000,928000000},
  {"ANZ_433",433050000,434790000},{"RU",868700000,869200000},
  {"KR",920000000,923000000},{"TW",920000000,925000000},
  {"IN",865000000,867000000},{"NZ_865",864000000,868000000},
  {"TH",920000000,925000000},{"UA_433",433000000,434700000},
  {"MY_433",433000000,435000000},{"MY_919",919000000,924000000},
  {"SG_923",917000000,925000000},{"PH_433",433000000,434700000},
  {"PH_868",868000000,869400000},{"PH_915",915000000,918000000},
  {"KZ_433",433075000,434775000},{"KZ_863",863000000,868000000},
  {"NP_865",865000000,868000000},{"BR_902",902000000,907500000}
}};

inline constexpr uint16_t slot_count(size_t index){
  const auto& r=kRegions[std::min(index,kRegions.size()-1)];
  return static_cast<uint16_t>((r.end_hz-r.start_hz)/kBandwidthHz);
}
inline constexpr uint32_t hash(){
  uint32_t h=5381;for(char c: {'L','o','n','g','F','a','s','t'})h=h*33u+c;return h;
}
inline constexpr uint16_t default_slot(size_t index){
  const auto count=slot_count(index);return count?static_cast<uint16_t>(hash()%count+1u):0;
}
inline constexpr uint32_t frequency_hz(size_t index,uint16_t slot){
  const auto& r=kRegions[std::min(index,kRegions.size()-1)];
  slot=std::clamp<uint16_t>(slot,1,slot_count(index));
  return r.start_hz+kBandwidthHz/2u+uint32_t(slot-1u)*kBandwidthHz;
}

} // namespace orcsdr::lora_plan

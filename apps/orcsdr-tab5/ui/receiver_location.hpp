#pragma once

#include <cstdint>

namespace orcsdr::receiver_location {

struct Snapshot {
  bool configured = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  char label[40]{};
  char map_pack[40]{};
  uint32_t revision = 0;
};

bool valid(bool configured, int32_t latitude_e7, int32_t longitude_e7);
void set(bool configured, int32_t latitude_e7, int32_t longitude_e7,
         const char* label = nullptr, const char* map_pack = nullptr);
Snapshot snapshot();
bool self_check();

}  // namespace orcsdr::receiver_location

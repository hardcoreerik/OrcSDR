#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::time_service {

struct Snapshot {
  uint32_t uptime_ms = 0;
  uint32_t utc = 0;
  bool wallclock_valid = false;
};

void initialize(bool previously_established);
Snapshot now();
bool set_utc(uint32_t epoch);
bool format_utc(char* output, size_t output_size, uint32_t epoch);
bool self_check();

}  // namespace orcsdr::time_service

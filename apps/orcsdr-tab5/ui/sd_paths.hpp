#pragma once
// Which SD paths the serial transfer commands may touch (SD_GET / SD_PUT / SD_REMOVE, and the RDS replay file).
// Pure functions, host-tested in tests/sd_paths_tests.cpp. Moved unchanged from main.cpp.
#include <cstddef>

namespace orcsdr::sd_paths {

constexpr size_t kMaxPath = 128;   // including the terminator; SD_PUT's target buffer

// Under /orcsdr/ with a name after it, shorter than kMaxPath, printable ASCII, no "..", no backslash.
bool transfer_allowed(const char* path);

// transfer_allowed(), plus the retired animated splash file at the card root.
bool remove_allowed(const char* path);

}  // namespace orcsdr::sd_paths

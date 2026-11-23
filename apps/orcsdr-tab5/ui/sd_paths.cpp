#include "sd_paths.hpp"

#include <cstring>

namespace orcsdr::sd_paths {

bool transfer_allowed(const char* path) {
  if (path == nullptr || strncmp(path, "/orcsdr/", 8) != 0 ||
      strlen(path) >= kMaxPath || strstr(path, "..") != nullptr ||
      strchr(path, '\\') != nullptr) {
    return false;
  }
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(path); *p; ++p) {
    if (*p < 0x20 || *p > 0x7e) return false;
  }
  return path[8] != '\0';
}

bool remove_allowed(const char* path) {
  // The retired animated splash lived at the card root; keep it removable.
  return transfer_allowed(path) ||
         strcmp(path, "/OrcSDR_Splash_1280x720_60fps_10s.orsplash") == 0;
}

}  // namespace orcsdr::sd_paths

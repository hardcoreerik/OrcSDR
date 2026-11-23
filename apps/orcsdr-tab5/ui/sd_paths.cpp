#include "sd_paths.hpp"

#include <cstring>

namespace orcsdr::sd_paths {

bool transfer_allowed(const char* path) {
  if (path == nullptr || strncmp(path, "/orcsdr/", 8) != 0 ||
      strlen(path) >= kMaxPath || strstr(path, "..") != nullptr ||
      strchr(path, '\\') != nullptr || strchr(path, '"') != nullptr) {
    return false;
  }
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(path); *p; ++p) {
    if (*p < 0x20 || *p > 0x7e) return false;
  }
  return path[8] != '\0';
}

bool remove_allowed(const char* path) {
  if (path == nullptr) return false;
  // The retired animated splash lived at the card root; keep it removable.
  return transfer_allowed(path) ||
         strcmp(path, "/OrcSDR_Splash_1280x720_60fps_10s.orsplash") == 0;
}

bool is_acars_log(const char* path) {
  if (!transfer_allowed(path) || strncmp(path, "/orcsdr/acars/", 14) != 0) {
    return false;
  }
  const char* name = path + 14;
  if (strcmp(name, "undated.jsonl") == 0) return true;
  if (strlen(name) != 14 || strcmp(name + 8, ".jsonl") != 0) return false;
  for (size_t i = 0; i < 8; ++i) {
    if (name[i] < '0' || name[i] > '9') return false;
  }
  return true;
}

}  // namespace orcsdr::sd_paths

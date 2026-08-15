#pragma once

#include <cstddef>
#include <cstdint>

#include <FS.h>

namespace orcsdr::fmconfig {

constexpr uint32_t kSchemaVersion = 1;
constexpr size_t kMaxPresets = 10;
constexpr char kPath[] = "/orcsdr/FM.cfg";

struct Config {
  uint32_t version = kSchemaVersion;
  uint32_t startup_frequency_hz = 0;
  uint32_t presets_hz[kMaxPresets]{};
  uint8_t preset_count = 0;
};

enum class LoadResult : uint8_t { ok, missing, invalid, io_error };

bool validate(const Config& config, char* error, size_t error_size);
LoadResult load(fs::FS& fs, const char* path, Config* config, char* error, size_t error_size);
bool save(fs::FS& fs, const Config& config, char* error, size_t error_size);
bool self_check();

}  // namespace orcsdr::fmconfig

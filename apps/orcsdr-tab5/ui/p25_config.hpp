#pragma once

#include <cstddef>
#include <cstdint>

#include "orcsdr_storage.hpp"

namespace orcsdr::p25config {

constexpr uint32_t kSchemaVersion = 1;
constexpr size_t kMaxControlChannels = 8;
constexpr size_t kMaxTalkgroups = 8;
constexpr char kPath[] = "/orcsdr/P25.cfg";

struct Talkgroup {
  uint16_t id = 0;
  char alias[32]{};
};

struct Config {
  uint32_t version = kSchemaVersion;
  char system_name[48]{};
  uint32_t control_channels_hz[kMaxControlChannels]{};
  uint8_t control_channel_count = 0;
  uint32_t last_control_channel_hz = 0;
  bool auto_follow = true;
  bool encryption_skip = true;
  uint16_t hold_talkgroup = 0;
  Talkgroup talkgroups[kMaxTalkgroups]{};
  uint8_t talkgroup_count = 0;
};

enum class LoadResult : uint8_t { ok, missing, invalid, io_error };

void defaults(Config* config);
bool validate(const Config& config, char* error, size_t error_size);
LoadResult load(orcsdr::storage::FileSystem& fs, const char* path, Config* config, char* error, size_t error_size);
bool save(orcsdr::storage::FileSystem& fs, const Config& config, char* error, size_t error_size);
bool self_check();

}  // namespace orcsdr::p25config

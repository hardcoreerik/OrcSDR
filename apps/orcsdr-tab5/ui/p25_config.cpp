#include "p25_config.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::p25config {
namespace {

constexpr uint32_t kP25MinHz = 450000000;
constexpr uint32_t kP25MaxHz = 470000000;

void set_error(char* error, size_t size, const char* value) {
  if (error == nullptr || size == 0) return;
  snprintf(error, size, "%s", value);
}

char* trim(char* value) {
  while (*value && isspace(static_cast<unsigned char>(*value))) ++value;
  char* end = value + strlen(value);
  while (end > value && isspace(static_cast<unsigned char>(end[-1]))) --end;
  *end = '\0';
  return value;
}

bool parse_uint(const char* value, uint32_t* output) {
  if (value == nullptr || *value == '\0') return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (*end != '\0' || parsed > UINT32_MAX) return false;
  *output = static_cast<uint32_t>(parsed);
  return true;
}

bool parse_bool(const char* value, bool* output) {
  if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
    *output = true;
    return true;
  }
  if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
    *output = false;
    return true;
  }
  return false;
}

bool has_channel(const Config& config, uint32_t frequency_hz) {
  for (size_t i = 0; i < config.control_channel_count; ++i)
    if (config.control_channels_hz[i] == frequency_hz) return true;
  return false;
}

bool has_talkgroup(const Config& config, uint16_t id) {
  for (size_t i = 0; i < config.talkgroup_count; ++i)
    if (config.talkgroups[i].id == id) return true;
  return false;
}

bool parse_text(const char* text, Config* config, char* error, size_t error_size) {
  Config parsed{};
  snprintf(parsed.system_name, sizeof(parsed.system_name), "%s", "My P25 System");
  bool has_version = false;
  uint32_t line_number = 0;
  const char* cursor = text;
  while (*cursor) {
    ++line_number;
    char line[128]{};
    size_t length = 0;
    while (*cursor && *cursor != '\n' && length + 1 < sizeof(line)) line[length++] = *cursor++;
    if (*cursor == '\n') ++cursor;
    if (*cursor && length + 1 == sizeof(line)) {
      set_error(error, error_size, "line too long");
      return false;
    }
    char* value = trim(line);
    if (*value == '\0' || *value == '#' || *value == ';') continue;
    char* equals = strchr(value, '=');
    if (equals == nullptr) {
      snprintf(error, error_size, "line %lu missing =", static_cast<unsigned long>(line_number));
      return false;
    }
    *equals = '\0';
    char* key = trim(value);
    char* field = trim(equals + 1);
    uint32_t number = 0;
    if (strcmp(key, "version") == 0) {
      if (!parse_uint(field, &number) || number != kSchemaVersion) {
        snprintf(error, error_size, "line %lu version", static_cast<unsigned long>(line_number));
        return false;
      }
      parsed.version = number;
      has_version = true;
    } else if (strcmp(key, "system_name") == 0) {
      if (*field == '\0' || strlen(field) >= sizeof(parsed.system_name)) {
        snprintf(error, error_size, "line %lu system_name", static_cast<unsigned long>(line_number));
        return false;
      }
      snprintf(parsed.system_name, sizeof(parsed.system_name), "%s", field);
    } else if (strcmp(key, "control_channel_hz") == 0) {
      if (!parse_uint(field, &number) || number < kP25MinHz || number > kP25MaxHz ||
          has_channel(parsed, number) || parsed.control_channel_count >= kMaxControlChannels) {
        snprintf(error, error_size, "line %lu control channel", static_cast<unsigned long>(line_number));
        return false;
      }
      parsed.control_channels_hz[parsed.control_channel_count++] = number;
    } else if (strcmp(key, "last_control_channel_hz") == 0) {
      if (!parse_uint(field, &number)) {
        snprintf(error, error_size, "line %lu last channel", static_cast<unsigned long>(line_number));
        return false;
      }
      parsed.last_control_channel_hz = number;
    } else if (strcmp(key, "auto_follow") == 0) {
      if (!parse_bool(field, &parsed.auto_follow)) {
        snprintf(error, error_size, "line %lu auto_follow", static_cast<unsigned long>(line_number));
        return false;
      }
    } else if (strcmp(key, "encryption_skip") == 0) {
      if (!parse_bool(field, &parsed.encryption_skip)) {
        snprintf(error, error_size, "line %lu encryption_skip", static_cast<unsigned long>(line_number));
        return false;
      }
    } else if (strcmp(key, "hold_talkgroup") == 0) {
      if (!parse_uint(field, &number) || number > UINT16_MAX) {
        snprintf(error, error_size, "line %lu hold_talkgroup", static_cast<unsigned long>(line_number));
        return false;
      }
      parsed.hold_talkgroup = static_cast<uint16_t>(number);
    } else if (strcmp(key, "talkgroup") == 0) {
      char* comma = strchr(field, ',');
      if (comma == nullptr) {
        snprintf(error, error_size, "line %lu talkgroup", static_cast<unsigned long>(line_number));
        return false;
      }
      *comma = '\0';
      char* alias = trim(comma + 1);
      if (!parse_uint(trim(field), &number) || number == 0 || number > UINT16_MAX ||
          *alias == '\0' || strlen(alias) >= sizeof(parsed.talkgroups[0].alias) ||
          has_talkgroup(parsed, static_cast<uint16_t>(number)) ||
          parsed.talkgroup_count >= kMaxTalkgroups) {
        snprintf(error, error_size, "line %lu talkgroup", static_cast<unsigned long>(line_number));
        return false;
      }
      Talkgroup& talkgroup = parsed.talkgroups[parsed.talkgroup_count++];
      talkgroup.id = static_cast<uint16_t>(number);
      snprintf(talkgroup.alias, sizeof(talkgroup.alias), "%s", alias);
    } else {
      snprintf(error, error_size, "line %lu unknown key", static_cast<unsigned long>(line_number));
      return false;
    }
  }
  if (!has_version) {
    set_error(error, error_size, "missing version");
    return false;
  }
  if (!validate(parsed, error, error_size)) return false;
  *config = parsed;
  return true;
}

}  // namespace

void defaults(Config* config) {
  if (config == nullptr) return;
  *config = {};
  config->version = kSchemaVersion;
  snprintf(config->system_name, sizeof(config->system_name), "%s", "Lane County P25");
  constexpr uint32_t channels[] = {453812500, 453925000, 460187500, 460312500};
  for (uint32_t frequency_hz : channels)
    config->control_channels_hz[config->control_channel_count++] = frequency_hz;
  config->last_control_channel_hz = channels[0];
  constexpr struct { uint16_t id; const char* alias; } talkgroups[] = {
      {20001, "LCSO DISP 1"}, {20003, "LCSO SEC 2"}, {20051, "EPD DISP"},
      {20101, "SPD DISP"}, {20204, "LCF East 8"}, {20391, "LCF Firecom 1"},
      {20411, "Eugene PW Disp"}, {20440, "SPW Ch 1"}};
  for (const auto& source : talkgroups) {
    Talkgroup& target = config->talkgroups[config->talkgroup_count++];
    target.id = source.id;
    snprintf(target.alias, sizeof(target.alias), "%s", source.alias);
  }
}

bool validate(const Config& config, char* error, size_t error_size) {
  if (config.version != kSchemaVersion || config.system_name[0] == '\0' ||
      config.control_channel_count == 0 || config.control_channel_count > kMaxControlChannels ||
      config.talkgroup_count > kMaxTalkgroups) {
    set_error(error, error_size, "invalid profile header");
    return false;
  }
  for (size_t i = 0; i < config.control_channel_count; ++i) {
    const uint32_t frequency_hz = config.control_channels_hz[i];
    if (frequency_hz < kP25MinHz || frequency_hz > kP25MaxHz) {
      set_error(error, error_size, "control channel range");
      return false;
    }
    for (size_t j = i + 1; j < config.control_channel_count; ++j)
      if (frequency_hz == config.control_channels_hz[j]) {
        set_error(error, error_size, "duplicate control channel");
        return false;
      }
  }
  if (config.last_control_channel_hz != 0 && !has_channel(config, config.last_control_channel_hz)) {
    set_error(error, error_size, "last channel not configured");
    return false;
  }
  for (size_t i = 0; i < config.talkgroup_count; ++i) {
    if (config.talkgroups[i].id == 0 || config.talkgroups[i].alias[0] == '\0') {
      set_error(error, error_size, "invalid talkgroup");
      return false;
    }
    for (size_t j = i + 1; j < config.talkgroup_count; ++j)
      if (config.talkgroups[i].id == config.talkgroups[j].id) {
        set_error(error, error_size, "duplicate talkgroup");
        return false;
      }
  }
  return true;
}

LoadResult load(orcsdr::storage::FileSystem& fs, const char* path, Config* config, char* error, size_t error_size) {
  if (config == nullptr || path == nullptr) return LoadResult::io_error;
  if (!fs.exists(path)) return LoadResult::missing;
  File file = fs.open(path, FILE_READ);
  if (!file) {
    set_error(error, error_size, "cannot open config");
    return LoadResult::io_error;
  }
  char text[2048]{};
  const size_t bytes = file.readBytes(text, sizeof(text) - 1);
  const bool truncated = file.available();
  file.close();
  if (truncated) {
    set_error(error, error_size, "config too large");
    return LoadResult::invalid;
  }
  return parse_text(text, config, error, error_size) ? LoadResult::ok : LoadResult::invalid;
}

bool save(orcsdr::storage::FileSystem& fs, const Config& config, char* error, size_t error_size) {
  if (!validate(config, error, error_size)) return false;
  char temporary[48]{};
  char backup[48]{};
  snprintf(temporary, sizeof(temporary), "%s.part", kPath);
  snprintf(backup, sizeof(backup), "%s.bak", kPath);
  fs.mkdir("/orcsdr");
  fs.remove(temporary);
  File file = fs.open(temporary, FILE_WRITE, true);
  if (!file) {
    set_error(error, error_size, "cannot create config");
    return false;
  }
  file.printf("# OrcSDR P25 profile — edit on a computer, then reload from Program.\n");
  file.printf("version=%lu\n", static_cast<unsigned long>(config.version));
  file.printf("system_name=%s\n", config.system_name);
  for (size_t i = 0; i < config.control_channel_count; ++i)
    file.printf("control_channel_hz=%lu\n", static_cast<unsigned long>(config.control_channels_hz[i]));
  file.printf("last_control_channel_hz=%lu\n", static_cast<unsigned long>(config.last_control_channel_hz));
  file.printf("auto_follow=%s\n", config.auto_follow ? "true" : "false");
  file.printf("encryption_skip=%s\n", config.encryption_skip ? "true" : "false");
  file.printf("hold_talkgroup=%u\n", config.hold_talkgroup);
  for (size_t i = 0; i < config.talkgroup_count; ++i)
    file.printf("talkgroup=%u,%s\n", config.talkgroups[i].id, config.talkgroups[i].alias);
  file.close();
  Config verified{};
  if (load(fs, temporary, &verified, error, error_size) != LoadResult::ok) {
    fs.remove(temporary);
    return false;
  }
  fs.remove(backup);
  const bool had_target = fs.exists(kPath);
  if (had_target && !fs.rename(kPath, backup)) {
    fs.remove(temporary);
    set_error(error, error_size, "cannot back up config");
    return false;
  }
  if (!fs.rename(temporary, kPath)) {
    if (had_target) fs.rename(backup, kPath);
    fs.remove(temporary);
    set_error(error, error_size, "cannot replace config");
    return false;
  }
  return true;
}

bool self_check() {
  Config config{};
  defaults(&config);
  char error[48]{};
  if (!validate(config, error, sizeof(error))) return false;
  constexpr char kEditedProfile[] =
      "version=1\n"
      "system_name=Test System\n"
      "control_channel_hz=460000000\n"
      "last_control_channel_hz=460000000\n"
      "auto_follow=false\n"
      "encryption_skip=true\n"
      "hold_talkgroup=42\n"
      "talkgroup=42,Dispatch\n";
  if (!parse_text(kEditedProfile, &config, error, sizeof(error)) ||
      config.control_channel_count != 1 || config.talkgroup_count != 1 ||
      config.auto_follow || config.talkgroups[0].id != 42) return false;
  constexpr char kBadProfile[] = "version=1\ncontrol_channel_hz=100\n";
  if (parse_text(kBadProfile, &config, error, sizeof(error))) return false;
  defaults(&config);
  config.control_channels_hz[1] = config.control_channels_hz[0];
  return !validate(config, error, sizeof(error));
}

}  // namespace orcsdr::p25config

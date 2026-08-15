#include "fm_config.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::fmconfig {
namespace {
constexpr uint32_t kFmMinHz = 87500000;
constexpr uint32_t kFmMaxHz = 108000000;

void set_error(char* error, size_t size, const char* value) {
  if (error != nullptr && size) snprintf(error, size, "%s", value);
}
char* trim(char* value) {
  while (*value && isspace(static_cast<unsigned char>(*value))) ++value;
  char* end = value + strlen(value);
  while (end > value && isspace(static_cast<unsigned char>(end[-1]))) --end;
  *end = '\0';
  return value;
}
bool parse_uint(const char* value, uint32_t* output) {
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (*value == '\0' || *end != '\0' || parsed > UINT32_MAX) return false;
  *output = static_cast<uint32_t>(parsed);
  return true;
}
bool has_preset(const Config& config, uint32_t frequency_hz) {
  for (size_t i = 0; i < config.preset_count; ++i)
    if (config.presets_hz[i] == frequency_hz) return true;
  return false;
}
bool parse_text(const char* text, Config* config, char* error, size_t error_size) {
  Config parsed{};
  bool version = false;
  uint32_t line_number = 0;
  const char* cursor = text;
  while (*cursor) {
    ++line_number;
    char line[96]{};
    size_t length = 0;
    while (*cursor && *cursor != '\n' && length + 1 < sizeof(line)) line[length++] = *cursor++;
    if (*cursor == '\n') ++cursor;
    if (*cursor && length + 1 == sizeof(line)) { set_error(error, error_size, "line too long"); return false; }
    char* key = trim(line);
    if (*key == '\0' || *key == '#' || *key == ';') continue;
    char* equals = strchr(key, '=');
    if (equals == nullptr) { snprintf(error, error_size, "line %lu missing =", static_cast<unsigned long>(line_number)); return false; }
    *equals = '\0';
    char* value = trim(equals + 1);
    key = trim(key);
    uint32_t number = 0;
    if (!parse_uint(value, &number)) { snprintf(error, error_size, "line %lu value", static_cast<unsigned long>(line_number)); return false; }
    if (strcmp(key, "version") == 0) {
      if (number != kSchemaVersion) { snprintf(error, error_size, "line %lu version", static_cast<unsigned long>(line_number)); return false; }
      version = true;
    } else if (strcmp(key, "startup_frequency_hz") == 0) {
      parsed.startup_frequency_hz = number;
    } else if (strcmp(key, "preset_hz") == 0) {
      if (parsed.preset_count >= kMaxPresets || has_preset(parsed, number)) { snprintf(error, error_size, "line %lu preset", static_cast<unsigned long>(line_number)); return false; }
      parsed.presets_hz[parsed.preset_count++] = number;
    } else { snprintf(error, error_size, "line %lu unknown key", static_cast<unsigned long>(line_number)); return false; }
  }
  if (!version || !validate(parsed, error, error_size)) return false;
  *config = parsed;
  return true;
}
}  // namespace

bool validate(const Config& config, char* error, size_t error_size) {
  if (config.version != kSchemaVersion || config.startup_frequency_hz < kFmMinHz ||
      config.startup_frequency_hz > kFmMaxHz || config.preset_count > kMaxPresets) {
    set_error(error, error_size, "invalid FM profile"); return false;
  }
  for (size_t i = 0; i < config.preset_count; ++i) {
    if (config.presets_hz[i] < kFmMinHz || config.presets_hz[i] > kFmMaxHz) {
      set_error(error, error_size, "preset range"); return false;
    }
    for (size_t j = i + 1; j < config.preset_count; ++j)
      if (config.presets_hz[i] == config.presets_hz[j]) { set_error(error, error_size, "duplicate preset"); return false; }
  }
  return true;
}

LoadResult load(fs::FS& fs, const char* path, Config* config, char* error, size_t error_size) {
  if (!fs.exists(path)) return LoadResult::missing;
  File file = fs.open(path, FILE_READ);
  if (!file) { set_error(error, error_size, "cannot open config"); return LoadResult::io_error; }
  char text[1024]{};
  const size_t bytes = file.readBytes(text, sizeof(text) - 1);
  const bool truncated = file.available();
  file.close();
  if (truncated) { set_error(error, error_size, "config too large"); return LoadResult::invalid; }
  return parse_text(text, config, error, error_size) ? LoadResult::ok : LoadResult::invalid;
}

bool save(fs::FS& fs, const Config& config, char* error, size_t error_size) {
  if (!validate(config, error, error_size)) return false;
  constexpr char temporary[] = "/orcsdr/FM.cfg.part";
  constexpr char backup[] = "/orcsdr/FM.cfg.bak";
  fs.mkdir("/orcsdr"); fs.remove(temporary);
  File file = fs.open(temporary, FILE_WRITE, true);
  if (!file) { set_error(error, error_size, "cannot create config"); return false; }
  file.printf("# OrcSDR FM profile — scan presets or edit on a computer.\nversion=1\nstartup_frequency_hz=%lu\n", static_cast<unsigned long>(config.startup_frequency_hz));
  for (size_t i = 0; i < config.preset_count; ++i) file.printf("preset_hz=%lu\n", static_cast<unsigned long>(config.presets_hz[i]));
  file.close();
  Config verified{};
  if (load(fs, temporary, &verified, error, error_size) != LoadResult::ok) { fs.remove(temporary); return false; }
  fs.remove(backup);
  const bool had_target = fs.exists(kPath);
  if (had_target && !fs.rename(kPath, backup)) { fs.remove(temporary); set_error(error, error_size, "cannot back up config"); return false; }
  if (!fs.rename(temporary, kPath)) { if (had_target) fs.rename(backup, kPath); fs.remove(temporary); set_error(error, error_size, "cannot replace config"); return false; }
  return true;
}

bool self_check() {
  constexpr char kGood[] = "version=1\nstartup_frequency_hz=96100000\npreset_hz=96100000\n";
  Config config{}; char error[32]{};
  if (!parse_text(kGood, &config, error, sizeof(error))) return false;
  config.presets_hz[1] = config.presets_hz[0]; config.preset_count = 2;
  return !validate(config, error, sizeof(error));
}
}  // namespace orcsdr::fmconfig

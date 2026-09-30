#include "atc_presets.hpp"

#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace orcsdr::atc {
namespace {

orcsdr::storage::FileSystem* g_filesystem = nullptr;
const char* g_path = nullptr;
bool g_v2 = false;

bool valid(const Preset& preset) {
  return preset.latitude_e7 >= -900000000 && preset.latitude_e7 <= 900000000 &&
         preset.longitude_e7 >= -1800000000 && preset.longitude_e7 <= 1800000000 &&
         preset.frequency_hz >= 118000000 && preset.frequency_hz <= 136975000 &&
         preset.label[0];
}

size_t split_tabs(char* line, char** fields, size_t capacity) {
  if (!line || !fields || capacity == 0) return 0;
  size_t count = 0;
  char* cursor = line;
  while (count < capacity) {
    fields[count++] = cursor;
    char* tab = std::strchr(cursor, '\t');
    if (!tab) break;
    *tab = '\0';
    cursor = tab + 1;
  }
  return count;
}

bool parse_v2(char* line, Preset* output) {
  char* fields[12]{};
  if (split_tabs(line, fields, 12) != 12 || std::strcmp(fields[0], "COM") != 0)
    return false;
  Preset preset{};
  char trailing = 0;
  long latitude = 0;
  long longitude = 0;
  unsigned long frequency = 0;
  if (std::sscanf(fields[1], "%ld%c", &latitude, &trailing) != 1 ||
      std::sscanf(fields[2], "%ld%c", &longitude, &trailing) != 1 ||
      std::sscanf(fields[3], "%lu%c", &frequency, &trailing) != 1 ||
      latitude < INT32_MIN || latitude > INT32_MAX ||
      longitude < INT32_MIN || longitude > INT32_MAX ||
      frequency > UINT32_MAX)
    return false;
  preset.latitude_e7 = static_cast<int32_t>(latitude);
  preset.longitude_e7 = static_cast<int32_t>(longitude);
  preset.frequency_hz = static_cast<uint32_t>(frequency);
  std::strncpy(preset.label, fields[11], sizeof(preset.label) - 1);
  if (!preset.label[0]) {
    if (fields[6][0])
      std::snprintf(preset.label, sizeof(preset.label), "%s %.20s", fields[6], fields[4]);
    else
      std::strncpy(preset.label, fields[4], sizeof(preset.label) - 1);
  }
  if (!valid(preset)) return false;
  *output = preset;
  return true;
}

bool open_and_validate(orcsdr::storage::FileSystem* filesystem, const char* path,
                       bool* v2) {
  if (!filesystem || !path || !v2) return false;
  auto file = filesystem->open(path);
  if (!file) return false;
  char header[16]{};
  const size_t used = file.readBytesUntil('\n', header, sizeof(header) - 1);
  header[used] = '\0';
  file.close();
  if (std::strcmp(header, "ORCAIR2") == 0) {
    *v2 = true;
    return true;
  }
  if (std::strcmp(header, "ORCCAT1") == 0) {
    *v2 = false;
    return true;
  }
  return false;
}

}  // namespace

bool load(orcsdr::storage::FileSystem* filesystem) {
  g_filesystem = nullptr;
  g_path = nullptr;
  g_v2 = false;
  bool v2 = false;
  if (open_and_validate(filesystem, kRuntimePath, &v2)) {
    g_filesystem = filesystem;
    g_path = kRuntimePath;
    g_v2 = v2;
    return true;
  }
  if (open_and_validate(filesystem, kLegacyRuntimePath, &v2)) {
    g_filesystem = filesystem;
    g_path = kLegacyRuntimePath;
    g_v2 = v2;
    return true;
  }
  return false;
}

bool nearest(int32_t latitude_e7, int32_t longitude_e7, Preset* output) {
  if (!output || !g_filesystem || !g_path) return false;
  auto file = g_filesystem->open(g_path);
  if (!file) return false;
  char line[320]{};
  (void)file.readBytesUntil('\n', line, sizeof(line) - 1);  // header
  bool found = false;
  Preset best{};
  double best_distance = INFINITY;
  while (true) {
    const size_t size = file.readBytesUntil('\n', line, sizeof(line) - 1);
    if (size == 0) break;
    line[size] = '\0';
    if (size > 0 && line[size - 1] == '\r') line[size - 1] = '\0';
    Preset preset{};
    bool parsed = false;
    if (g_v2) {
      parsed = parse_v2(line, &preset);
    } else {
      parsed = std::sscanf(line, "ATC %" SCNd32 " %" SCNd32 " %" SCNu32 " %31[^\n]",
                           &preset.latitude_e7, &preset.longitude_e7,
                           &preset.frequency_hz, preset.label) == 4 &&
               valid(preset);
    }
    if (!parsed) continue;
    const double lat = static_cast<double>(preset.latitude_e7) - latitude_e7;
    const double lon = static_cast<double>(preset.longitude_e7) - longitude_e7;
    const double distance = lat * lat + lon * lon;
    if (distance < best_distance) {
      best_distance = distance;
      best = preset;
      found = true;
    }
  }
  file.close();
  if (!found) return false;
  *output = best;
  return true;
}

bool self_check() {
  Preset sample{440000000, -1230000000, 118900000, "TEST"};
  char v2[] =
      "COM\t135019000\t1039940000\t118600000\tTOWER\tSG\tWSSS\t"
      "Singapore Changi Airport\t\tCOMMUNITY\tOURAIRPORTS\tWSSS TOWER";
  Preset parsed{};
  return valid(sample) && !valid({0, 0, 117000000, "BAD"}) &&
         parse_v2(v2, &parsed) && parsed.frequency_hz == 118600000u &&
         std::strcmp(parsed.label, "WSSS TOWER") == 0;
}

}  // namespace orcsdr::atc

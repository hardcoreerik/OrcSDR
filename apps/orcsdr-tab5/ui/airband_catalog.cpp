#include "airband_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::airband {
namespace {

constexpr double kEarthRadiusNm = 3440.065;
constexpr double kPi = 3.14159265358979323846;
constexpr size_t kLineCapacity = 320;

bool valid_entry(const CatalogEntry& entry) {
  return entry.latitude_e7 >= -900000000 && entry.latitude_e7 <= 900000000 &&
         entry.longitude_e7 >= -1800000000 && entry.longitude_e7 <= 1800000000 &&
         in_band(entry.frequency_hz) && entry.label[0] != '\0';
}

void copy_text(char* output, size_t size, const char* value) {
  if (size == 0) return;
  if (value == nullptr) {
    output[0] = '\0';
    return;
  }
  std::strncpy(output, value, size - 1);
  output[size - 1] = '\0';
}

bool contains_word(const char* value, const char* token) {
  if (!value || !token || !token[0]) return false;
  char upper[96]{};
  size_t i = 0;
  for (; value[i] && i + 1 < sizeof(upper); ++i) {
    const char c = value[i];
    upper[i] = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
  }
  upper[i] = '\0';

  const size_t token_len = std::strlen(token);
  const char* cursor = upper;
  while ((cursor = std::strstr(cursor, token)) != nullptr) {
    const char before = cursor == upper ? '\0' : cursor[-1];
    const char after = cursor[token_len];
    const auto alpha_num = [](char ch) {
      return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    };
    if (!alpha_num(before) && !alpha_num(after)) return true;
    ++cursor;
  }
  return false;
}

float distance_nm(int32_t lat_a_e7, int32_t lon_a_e7,
                  int32_t lat_b_e7, int32_t lon_b_e7) {
  const double lat1 = static_cast<double>(lat_a_e7) / 10000000.0 * kPi / 180.0;
  const double lat2 = static_cast<double>(lat_b_e7) / 10000000.0 * kPi / 180.0;
  const double dlat = lat2 - lat1;
  const double dlon =
      (static_cast<double>(lon_b_e7 - lon_a_e7) / 10000000.0) * kPi / 180.0;
  const double sin_lat = std::sin(dlat / 2.0);
  const double sin_lon = std::sin(dlon / 2.0);
  const double a = sin_lat * sin_lat +
                   std::cos(lat1) * std::cos(lat2) * sin_lon * sin_lon;
  const double clamped = std::min(1.0, std::max(0.0, a));
  return static_cast<float>(2.0 * kEarthRadiusNm * std::asin(std::sqrt(clamped)));
}

Service parse_service(const char* value) {
  if (!value || !value[0]) return Service::unknown;
  if (std::strcmp(value, "TOWER") == 0 || std::strcmp(value, "TWR") == 0)
    return Service::tower;
  if (std::strcmp(value, "GROUND") == 0 || std::strcmp(value, "GND") == 0)
    return Service::ground;
  if (std::strcmp(value, "APPROACH") == 0 || std::strcmp(value, "APP") == 0)
    return Service::approach;
  if (std::strcmp(value, "DEPARTURE") == 0 || std::strcmp(value, "DEP") == 0)
    return Service::departure;
  if (std::strcmp(value, "CENTER") == 0 || std::strcmp(value, "ACC") == 0)
    return Service::center;
  if (std::strcmp(value, "ATIS") == 0) return Service::atis;
  if (std::strcmp(value, "AWOS") == 0 || std::strcmp(value, "ASOS") == 0)
    return Service::awos;
  if (std::strcmp(value, "CTAF") == 0) return Service::ctaf;
  if (std::strcmp(value, "UNICOM") == 0) return Service::unicom;
  if (std::strcmp(value, "CLEARANCE") == 0 || std::strcmp(value, "CLNC") == 0 ||
      std::strcmp(value, "DEL") == 0)
    return Service::clearance;
  if (std::strcmp(value, "EMERGENCY") == 0 || std::strcmp(value, "GUARD") == 0)
    return Service::emergency;
  return Service::unknown;
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

bool parse_i32(const char* value, int32_t* output) {
  if (!value || !output || !value[0]) return false;
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (*end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) return false;
  *output = static_cast<int32_t>(parsed);
  return true;
}

bool parse_u32(const char* value, uint32_t* output) {
  if (!value || !output || !value[0] || value[0] == '-') return false;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (*end != '\0' || parsed > UINT32_MAX) return false;
  *output = static_cast<uint32_t>(parsed);
  return true;
}

bool parse_v2(char* line, CatalogEntry* output) {
  // ORCAIR2 row:
  // COM <lat_e7> <lon_e7> <hz> <service> <country> <ident> <airport_name>
  //     <callsign> <source_class> <source_name> <label>
  // Fields are tab-delimited so descriptive text may contain spaces.
  char* fields[12]{};
  if (split_tabs(line, fields, std::size(fields)) != std::size(fields) ||
      std::strcmp(fields[0], "COM") != 0)
    return false;
  CatalogEntry entry{};
  if (!parse_i32(fields[1], &entry.latitude_e7) ||
      !parse_i32(fields[2], &entry.longitude_e7) ||
      !parse_u32(fields[3], &entry.frequency_hz))
    return false;
  entry.service = parse_service(fields[4]);
  copy_text(entry.country, sizeof(entry.country), fields[5]);
  copy_text(entry.airport_ident, sizeof(entry.airport_ident), fields[6]);
  copy_text(entry.airport_name, sizeof(entry.airport_name), fields[7]);
  copy_text(entry.callsign, sizeof(entry.callsign), fields[8]);
  entry.source_class = parse_source_class(fields[9]);
  copy_text(entry.source, sizeof(entry.source), fields[10]);
  copy_text(entry.label, sizeof(entry.label), fields[11]);
  if (!entry.label[0]) {
    if (entry.airport_ident[0]) {
      std::snprintf(entry.label, sizeof(entry.label), "%s %s", entry.airport_ident,
                    service_name(entry.service));
    } else {
      copy_text(entry.label, sizeof(entry.label), service_name(entry.service));
    }
  }
  if (entry.service == Service::unknown) entry.service = classify_service(entry.label);
  if (!valid_entry(entry)) return false;
  *output = entry;
  return true;
}

}  // namespace

const char* service_name(Service service) {
  switch (service) {
    case Service::tower: return "TOWER";
    case Service::ground: return "GROUND";
    case Service::approach: return "APPROACH";
    case Service::departure: return "DEPARTURE";
    case Service::center: return "CENTER";
    case Service::atis: return "ATIS";
    case Service::awos: return "AWOS";
    case Service::ctaf: return "CTAF";
    case Service::unicom: return "UNICOM";
    case Service::clearance: return "CLEARANCE";
    case Service::emergency: return "EMERGENCY";
    case Service::unknown: break;
  }
  return "AIRBAND";
}

Service classify_service(const char* label) {
  if (contains_word(label, "TOWER") || contains_word(label, "TWR"))
    return Service::tower;
  if (contains_word(label, "GROUND") || contains_word(label, "GND"))
    return Service::ground;
  if (contains_word(label, "APPROACH") || contains_word(label, "APP"))
    return Service::approach;
  if (contains_word(label, "DEPARTURE") || contains_word(label, "DEP"))
    return Service::departure;
  if (contains_word(label, "CENTER") || contains_word(label, "ARTCC") ||
      contains_word(label, "ACC"))
    return Service::center;
  if (contains_word(label, "ATIS")) return Service::atis;
  if (contains_word(label, "AWOS") || contains_word(label, "ASOS"))
    return Service::awos;
  if (contains_word(label, "CTAF")) return Service::ctaf;
  if (contains_word(label, "UNICOM")) return Service::unicom;
  if (contains_word(label, "CLEARANCE") || contains_word(label, "CLNC") ||
      contains_word(label, "DELIVERY"))
    return Service::clearance;
  if (contains_word(label, "EMERGENCY") || contains_word(label, "GUARD"))
    return Service::emergency;
  return Service::unknown;
}

const char* source_class_name(SourceClass source_class) {
  switch (source_class) {
    case SourceClass::official: return "OFFICIAL";
    case SourceClass::licensed: return "LICENSED";
    case SourceClass::community: return "COMMUNITY";
    case SourceClass::user: return "USER";
    case SourceClass::derived: return "DERIVED";
    case SourceClass::unknown: break;
  }
  return "UNKNOWN";
}

SourceClass parse_source_class(const char* value) {
  if (!value) return SourceClass::unknown;
  if (std::strcmp(value, "OFFICIAL") == 0) return SourceClass::official;
  if (std::strcmp(value, "LICENSED") == 0) return SourceClass::licensed;
  if (std::strcmp(value, "COMMUNITY") == 0) return SourceClass::community;
  if (std::strcmp(value, "USER") == 0) return SourceClass::user;
  if (std::strcmp(value, "DERIVED") == 0) return SourceClass::derived;
  return SourceClass::unknown;
}

void Catalog::clear() {
  count_ = 0;
  loaded_ = false;
  location_configured_ = false;
  global_schema_ = false;
  for (auto& entry : entries_) entry = {};
}

void Catalog::consider(const CatalogEntry& entry, bool use_distance) {
  if (!valid_entry(entry)) return;

  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].frequency_hz == entry.frequency_hz &&
        entries_[i].latitude_e7 == entry.latitude_e7 &&
        entries_[i].longitude_e7 == entry.longitude_e7 &&
        std::strncmp(entries_[i].label, entry.label, sizeof(entry.label)) == 0) {
      if (use_distance && entry.distance_nm < entries_[i].distance_nm)
        entries_[i] = entry;
      return;
    }
  }

  if (count_ < kCapacity) {
    entries_[count_++] = entry;
    return;
  }

  if (!use_distance) return;
  size_t worst = 0;
  for (size_t i = 1; i < count_; ++i)
    if (entries_[i].distance_nm > entries_[worst].distance_nm) worst = i;
  if (entry.distance_nm < entries_[worst].distance_nm) entries_[worst] = entry;
}

void Catalog::sort() {
  std::sort(entries_, entries_ + count_, [](const CatalogEntry& a, const CatalogEntry& b) {
    if (a.distance_nm != b.distance_nm) return a.distance_nm < b.distance_nm;
    if (a.frequency_hz != b.frequency_hz) return a.frequency_hz < b.frequency_hz;
    return std::strcmp(a.label, b.label) < 0;
  });
}

bool Catalog::load(storage::FileSystem* filesystem, const Location& location) {
  clear();
  location_configured_ = location.configured;
  if (!filesystem) return false;
  storage::File file = filesystem->open(kCatalogPath);
  if (!file) file = filesystem->open(kLegacyCatalogPath);
  if (!file) return false;

  char line[kLineCapacity]{};
  const size_t header_size = file.readBytesUntil('\n', line, sizeof(line) - 1);
  line[header_size] = '\0';
  const bool v2 = std::strcmp(line, "ORCAIR2") == 0;
  const bool legacy = std::strcmp(line, "ORCCAT1") == 0;
  if (!v2 && !legacy) {
    file.close();
    return false;
  }
  global_schema_ = v2;

  while (true) {
    const size_t size = file.readBytesUntil('\n', line, sizeof(line) - 1);
    if (size == 0) break;
    line[size] = '\0';
    if (size > 0 && line[size - 1] == '\r') line[size - 1] = '\0';

    CatalogEntry entry{};
    bool parsed = false;
    if (v2) {
      parsed = parse_v2(line, &entry);
    } else if (std::sscanf(line, "ATC %" SCNd32 " %" SCNd32 " %" SCNu32 " %39[^\n]",
                           &entry.latitude_e7, &entry.longitude_e7,
                           &entry.frequency_hz, entry.label) == 4) {
      entry.service = classify_service(entry.label);
      entry.source_class = SourceClass::official;
      copy_text(entry.source, sizeof(entry.source), "FAA");
      parsed = valid_entry(entry);
    }
    if (!parsed) continue;
    entry.distance_nm = location.configured
                            ? distance_nm(location.latitude_e7, location.longitude_e7,
                                          entry.latitude_e7, entry.longitude_e7)
                            : static_cast<float>(count_);
    consider(entry, location.configured);
  }
  file.close();
  sort();
  loaded_ = count_ != 0;
  return loaded_;
}

size_t Catalog::make_bank(BankEntry* output, size_t capacity) const {
  if (!location_configured_ || !output || capacity == 0) return 0;
  size_t count = 0;
  for (size_t i = 0; i < count_ && count < capacity; ++i) {
    const CatalogEntry& source = entries_[i];
    bool duplicate = false;
    for (size_t j = 0; j < count; ++j)
      duplicate |= output[j].frequency_hz == source.frequency_hz;
    if (duplicate) continue;
    output[count].frequency_hz = source.frequency_hz;
    output[count].distance_nm = source.distance_nm;
    std::strncpy(output[count].label, source.label, sizeof(output[count].label) - 1);
    ++count;
  }
  return count;
}

const CatalogEntry* Catalog::match(uint32_t frequency_hz) const {
  if (!location_configured_) return nullptr;
  const CatalogEntry* best = nullptr;
  uint32_t best_delta = UINT32_MAX;
  for (size_t i = 0; i < count_; ++i) {
    const uint32_t hz = entries_[i].frequency_hz;
    const uint32_t delta = hz > frequency_hz ? hz - frequency_hz : frequency_hz - hz;
    if (delta < best_delta) {
      best = &entries_[i];
      best_delta = delta;
    }
  }
  return best_delta <= 5000u ? best : nullptr;
}

bool Catalog::self_check() {
  return classify_service("KEUG TOWER") == Service::tower &&
         classify_service("PORTLAND ATIS") == Service::atis &&
         classify_service("REGIONAL ASOS") == Service::awos &&
         classify_service("SINGAPORE DELIVERY") == Service::clearance &&
         classify_service("SOMETHING") == Service::unknown &&
         parse_source_class("OFFICIAL") == SourceClass::official &&
         parse_source_class("COMMUNITY") == SourceClass::community &&
         std::strcmp(source_class_name(SourceClass::licensed), "LICENSED") == 0 &&
         std::strcmp(service_name(Service::ground), "GROUND") == 0;
}

}  // namespace orcsdr::airband

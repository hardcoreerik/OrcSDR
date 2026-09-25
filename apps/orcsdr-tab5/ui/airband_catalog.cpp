#include "airband_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace orcsdr::airband {
namespace {

constexpr double kEarthRadiusNm = 3440.065;
constexpr double kPi = 3.14159265358979323846;

bool valid_entry(const CatalogEntry& entry) {
  return entry.latitude_e7 >= -900000000 && entry.latitude_e7 <= 900000000 &&
         entry.longitude_e7 >= -1800000000 && entry.longitude_e7 <= 1800000000 &&
         in_band(entry.frequency_hz) && entry.label[0] != '\0';
}

bool contains_token(const char* value, const char* token) {
  if (!value || !token) return false;
  char upper[64]{};
  size_t i = 0;
  for (; value[i] && i + 1 < sizeof(upper); ++i) {
    const char c = value[i];
    upper[i] = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
  }
  upper[i] = '\0';
  return std::strstr(upper, token) != nullptr;
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
  if (contains_token(label, "TOWER") || contains_token(label, "TWR"))
    return Service::tower;
  if (contains_token(label, "GROUND") || contains_token(label, "GND"))
    return Service::ground;
  if (contains_token(label, "APPROACH") || contains_token(label, "APP"))
    return Service::approach;
  if (contains_token(label, "DEPARTURE") || contains_token(label, "DEP"))
    return Service::departure;
  if (contains_token(label, "CENTER") || contains_token(label, "ARTCC"))
    return Service::center;
  if (contains_token(label, "ATIS")) return Service::atis;
  if (contains_token(label, "AWOS") || contains_token(label, "ASOS"))
    return Service::awos;
  if (contains_token(label, "CTAF")) return Service::ctaf;
  if (contains_token(label, "UNICOM")) return Service::unicom;
  if (contains_token(label, "CLEARANCE") || contains_token(label, "CLNC"))
    return Service::clearance;
  if (contains_token(label, "EMERGENCY") || contains_token(label, "GUARD"))
    return Service::emergency;
  return Service::unknown;
}

void Catalog::clear() {
  count_ = 0;
  loaded_ = false;
  for (auto& entry : entries_) entry = {};
}

void Catalog::consider(const CatalogEntry& entry, bool use_distance) {
  if (!valid_entry(entry)) return;

  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].frequency_hz == entry.frequency_hz &&
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
  if (!filesystem) return false;
  storage::File file = filesystem->open(kCatalogPath);
  if (!file) return false;

  char line[128]{};
  const size_t header_size = file.readBytesUntil('\n', line, sizeof(line) - 1);
  line[header_size] = '\0';
  if (std::strcmp(line, "ORCCAT1") != 0) {
    file.close();
    return false;
  }

  while (true) {
    const size_t size = file.readBytesUntil('\n', line, sizeof(line) - 1);
    if (size == 0) break;
    line[size] = '\0';

    CatalogEntry entry{};
    if (std::sscanf(line, "ATC %" SCNd32 " %" SCNd32 " %" SCNu32 " %39[^\n]",
                    &entry.latitude_e7, &entry.longitude_e7,
                    &entry.frequency_hz, entry.label) != 4)
      continue;
    if (!valid_entry(entry)) continue;
    entry.service = classify_service(entry.label);
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
  if (!output || capacity == 0) return 0;
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
         classify_service("SOMETHING") == Service::unknown &&
         std::strcmp(service_name(Service::ground), "GROUND") == 0;
}

}  // namespace orcsdr::airband

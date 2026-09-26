#pragma once

#include "airband_scanner.hpp"
#include "orcsdr_storage.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::airband {

constexpr const char kCatalogPath[] = "/orcsdr/data/aviation.idx";
constexpr const char kLegacyCatalogPath[] = "/orcsdr/data/faa_aviation.idx";

enum class Service : uint8_t {
  unknown,
  tower,
  ground,
  approach,
  departure,
  center,
  atis,
  awos,
  ctaf,
  unicom,
  clearance,
  emergency,
};

enum class SourceClass : uint8_t {
  unknown,
  official,
  licensed,
  community,
  user,
  derived,
};

struct CatalogEntry {
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint32_t frequency_hz = 0;
  float distance_nm = 0.0f;
  Service service = Service::unknown;
  SourceClass source_class = SourceClass::unknown;
  char country[3]{};
  char airport_ident[9]{};
  char airport_name[48]{};
  char callsign[32]{};
  char source[24]{};
  char label[40]{};
};

struct Location {
  bool configured = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
};

const char* service_name(Service service);
Service classify_service(const char* label);
const char* source_class_name(SourceClass source_class);
SourceClass parse_source_class(const char* value);

class Catalog {
 public:
  static constexpr size_t kCapacity = 32;

  bool load(storage::FileSystem* filesystem, const Location& location);
  void clear();
  bool loaded() const { return loaded_; }
  bool location_configured() const { return location_configured_; }
  bool global_schema() const { return global_schema_; }
  size_t count() const { return count_; }
  const CatalogEntry* entry(size_t index) const {
    return index < count_ ? &entries_[index] : nullptr;
  }

  // Airport-aware banks and labels are only enabled when a receiver location
  // is configured. This avoids presenting an arbitrary first record from a
  // national/global pack as if it were local RF identity.
  size_t make_bank(BankEntry* output, size_t capacity) const;
  const CatalogEntry* match(uint32_t frequency_hz) const;

  static bool self_check();

 private:
  void consider(const CatalogEntry& entry, bool use_distance);
  void sort();

  CatalogEntry entries_[kCapacity]{};
  size_t count_ = 0;
  bool loaded_ = false;
  bool location_configured_ = false;
  bool global_schema_ = false;
};

}  // namespace orcsdr::airband

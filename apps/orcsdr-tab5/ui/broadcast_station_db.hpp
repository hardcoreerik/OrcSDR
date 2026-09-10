#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::storage { class FileSystem; }

namespace orcsdr::broadcast {

enum class Service : uint8_t { unknown, am, fm, shortwave };

// Factual source fields only. `live_rds_agrees` is supplied by the FM runtime;
// a lookup itself never claims that a received signal is identified.
struct StationCard {
  uint32_t frequency_hz = 0;
  uint32_t power_w = 0;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  Service service = Service::unknown;
  bool live_rds_agrees = false;
  char id[96]{};
  char source[32]{};
  char source_id[32]{};
  char callsign[16]{};
  char name[64]{};
  char city[32]{};
  char region[16]{};
  char country[4]{};
  char status[16]{};
  char mode[8]{};
  char language[24]{};
  char utc_start[5]{};
  char utc_stop[5]{};
  char days[8]{};
  char season[8]{};
  char transmitter[32]{};
  char target[32]{};
  char rds_ps[9]{};
  char rds_pi[5]{};
};

class Database {
 public:
  static constexpr const char* kDefaultPath = "/orcsdr/data/broadcast_stations.idx";

  void begin(orcsdr::storage::FileSystem* filesystem, const char* path = kDefaultPath);
  bool refresh();
  bool available() const { return available_; }
  uint32_t record_count() const { return record_count_; }

  // Exact-frequency lookup. The caller supplies bounded storage; this code is
  // for normal UI/data work and must never run in RF, IQ, or audio callbacks.
  size_t lookup_frequency(uint32_t frequency_hz, StationCard* output, size_t capacity) const;

 private:
  orcsdr::storage::FileSystem* filesystem_ = nullptr;
  const char* path_ = kDefaultPath;
  uint32_t record_count_ = 0;
  uint32_t index_offset_ = 0;
  uint32_t records_offset_ = 0;
  bool available_ = false;
};

const char* service_label(Service service);

}  // namespace orcsdr::broadcast

#pragma once
// Bounded AIS station state. The receiver owns this store; UI consumers copy snapshots.
// No UI/map dependency. Type 28 Source IDs have their own namespace, never collide with MMSIs.
#include "ais_protocol.hpp"
#include <cstddef>
#include <cstdint>

namespace orcsdr::ais {

enum class IdentityKind : uint8_t { mmsi, source_id28 };
enum class StationKind : uint8_t { class_a, class_b, aid21_physical, aid21_virtual, aid28, long_range, base_station, sar_aircraft };
struct Fix { int32_t lat_e7 = 0, lon_e7 = 0; uint64_t received_ms = 0; };

struct Station {
  uint32_t identity = 0;
  IdentityKind identity_kind = IdentityKind::mmsi;
  StationKind kind = StationKind::class_a;
  uint8_t last_type = 0;
  uint64_t last_rx_ms = 0, last_position_ms = 0, last_static_ms = 0;
  bool has_position = false, has_speed = false, has_course = false, has_heading = false;
  int32_t lat_e7 = 0, lon_e7 = 0;
  uint16_t speed_tenths_kn = 0, course_tenths_deg = 0, heading_deg = 0;
  char name[37]{};
  // Static identity, from Types 5, 19 and 24. Empty / false until the station has sent it.
  char callsign[8]{}, destination[21]{};
  bool has_ship_type = false, has_nav_status = false;
  uint8_t ship_type = 0, nav_status = 0;
  Aid aid{}; // Type 28 geometric field values remain uninterpreted; not a projected polygon.
  Fix history[8]{};
  uint8_t history_size = 0;
};

class StationStore {
 public:
  static constexpr size_t kCapacity = 64, kHistory = 8;
  bool ingest(const Message& message, uint64_t received_ms);
  const Station* find(uint32_t identity, IdentityKind kind = IdentityKind::mmsi) const;
  const Station* at(size_t index) const { return index < size_ ? &stations_[index] : nullptr; }
  size_t size() const { return size_; }
  bool position_stale(const Station& station, uint64_t now_ms, uint64_t max_age_ms) const;
  void prune(uint64_t now_ms, uint64_t max_idle_ms);
  void clear() { size_ = 0; }
 private:
  Station stations_[kCapacity]{};
  size_t size_ = 0;
};

} // namespace orcsdr::ais

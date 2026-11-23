#include "ais_station_store.hpp"
#include <cstring>

namespace orcsdr::ais {
namespace {
bool static_only(const Message& m) {
  return m.coverage == Coverage::static_voyage || m.coverage == Coverage::static_b;
}
bool position_family(const Message& m) {   // every coverage that describes a station
  return m.coverage != Coverage::header_only;
}
StationKind station_kind(const Message& m) {
  if (m.coverage == Coverage::aid28) return StationKind::aid28;
  if (m.coverage == Coverage::aid21)
    return m.aid.is_virtual ? StationKind::aid21_virtual : StationKind::aid21_physical;
  if (m.coverage == Coverage::long_range27) return StationKind::long_range;
  if (m.coverage == Coverage::base_station) return StationKind::base_station;
  if (m.coverage == Coverage::sar_aircraft) return StationKind::sar_aircraft;
  if (m.coverage == Coverage::static_b) return StationKind::class_b;   // only Class B sends Type 24
  return m.type == 18 || m.type == 19 ? StationKind::class_b : StationKind::class_a;
}
} // namespace

const Station* StationStore::find(uint32_t identity, IdentityKind kind) const {
  for (size_t i = 0; i < size_; ++i)
    if (stations_[i].identity == identity && stations_[i].identity_kind == kind) return &stations_[i];
  return nullptr;
}

bool StationStore::ingest(const Message& message, uint64_t received_ms) {
  if (!position_family(message) || !message.source_id) return false;
  const IdentityKind identity_kind = message.coverage == Coverage::aid28
                                       ? IdentityKind::source_id28 : IdentityKind::mmsi;
  size_t i = 0;
  while (i < size_ && !(stations_[i].identity == message.source_id &&
                        stations_[i].identity_kind == identity_kind)) ++i;
  if (i < size_ && received_ms < stations_[i].last_rx_ms) return false;
  const bool created = i == size_;
  if (created) {
    if (size_ == kCapacity) return false; // No silent eviction or invented persistence.
    stations_[size_++] = Station{};
    stations_[i].identity = message.source_id;
    stations_[i].identity_kind = identity_kind;
  }
  Station& station = stations_[i];
  station.last_type = message.type;
  station.last_rx_ms = received_ms;
  // Long-range reports do not erase a more specific known Class A/B identity, and a static report
  // never changes the class a position report established.
  if (created || (!static_only(message) &&
                  (message.coverage != Coverage::long_range27 || station.history_size == 0)))
    station.kind = station_kind(message);
  // Static identity is kept until the station sends a different value; an absent field never clears a known one.
  const VesselStatic& v = message.vessel;
  if (v.has_name) {
    std::memset(station.name,0,sizeof(station.name));
    std::memcpy(station.name,v.name,sizeof(v.name));
    station.last_static_ms = received_ms;
  }
  if (v.has_callsign) { std::memcpy(station.callsign,v.callsign,sizeof(station.callsign)); station.last_static_ms = received_ms; }
  if (v.has_destination) {
    std::memcpy(station.destination,v.destination,sizeof(station.destination));
    station.last_static_ms = received_ms;
  }
  if (v.has_ship_type) { station.has_ship_type = true; station.ship_type = v.ship_type; }
  if (message.has_nav_status) { station.has_nav_status = true; station.nav_status = message.nav_status; }
  if (message.coverage == Coverage::aid21 || message.coverage == Coverage::aid28) {
    station.aid = message.aid;
    if (message.coverage == Coverage::aid21 && message.aid.name[0]) {
      std::memcpy(station.name,message.aid.name,sizeof(station.name));
      station.name[sizeof(station.name)-1] = '\0';
      station.last_static_ms = received_ms;
    }
  }
  if (message.has_position) {
    const bool changed = !station.has_position || station.lat_e7 != message.latitude_e7 ||
                         station.lon_e7 != message.longitude_e7;
    station.has_position = true;
    station.lat_e7 = message.latitude_e7;
    station.lon_e7 = message.longitude_e7;
    station.last_position_ms = received_ms;
    if (changed) {
      if (station.history_size == kHistory) {
        for (size_t j = 1; j < kHistory; ++j) station.history[j-1] = station.history[j];
        --station.history_size;
      }
      station.history[station.history_size++] = {message.latitude_e7,message.longitude_e7,received_ms};
    }
  }
  // An unavailable value is unknown, not zero and not a replacement for previously valid motion.
  if (message.has_speed) {
    station.has_speed = true; station.speed_tenths_kn = message.speed_tenths_kn;
  }
  if (message.has_course) {
    station.has_course = true; station.course_tenths_deg = message.course_tenths_deg;
  }
  if (message.has_heading) {
    station.has_heading = true; station.heading_deg = message.heading_deg;
  }
  return true;
}

bool StationStore::position_stale(const Station& station, uint64_t now_ms, uint64_t max_age_ms) const {
  return !station.has_position || now_ms < station.last_position_ms ||
         now_ms - station.last_position_ms > max_age_ms;
}

void StationStore::prune(uint64_t now_ms, uint64_t max_idle_ms) {
  size_t output = 0;
  for (size_t i = 0; i < size_; ++i) {
    const Station& s = stations_[i];
    if (now_ms < s.last_rx_ms || now_ms - s.last_rx_ms > max_idle_ms) continue;
    if (output != i) stations_[output] = s;
    ++output;
  }
  size_ = output;
}
} // namespace orcsdr::ais

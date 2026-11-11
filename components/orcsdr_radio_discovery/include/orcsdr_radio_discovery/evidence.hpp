#pragma once

#include <cstddef>
#include <cstdint>

// Host-portable evidence contract; does not own a tuner, decoder, UI, or filesystem.
// Directory entries, user edits, and RF observations deliberately have different origins.
namespace orcsdr::radio_discovery {

enum class Protocol : uint8_t {
  unknown, p25_phase1, p25_phase2, dmr_tier2, nxdn48, nxdn96
};
enum class Origin : uint8_t { rf_observation, directory_import, user_entry };
enum class Proof : uint8_t {
  candidate, energy_observed, protocol_signature, crc_valid_decode, site_confirmed
};

enum KnownField : uint16_t {
  access_code_known = 1u << 0, // DMR color code, P25 NAC, NXDN RAN: never interchangeable
  slot_known = 1u << 1,
  system_known = 1u << 2,
  site_known = 1u << 3,
  source_known = 1u << 4,
  destination_known = 1u << 5
};
constexpr uint16_t kKnownFieldMask = access_code_known | slot_known | system_known |
                                     site_known | source_known | destination_known;

struct ProtocolIdentity {
  uint32_t system_id = 0;      // Protocol-local namespace, not globally unique
  uint32_t site_id = 0;        // Protocol-local namespace, not GPS coordinates
  uint32_t source_id = 0;
  uint32_t destination_id = 0; // Call/talkgroup identity, when decoded
  uint16_t access_code = 0;
  uint16_t known_fields = 0;   // Zero is a possible valid identifier; consult flags
  uint8_t slot = 0;           // DMR TS1/TS2 = 1/2; P25 Phase II = 0/1
};

struct ReceiverLocation {
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  bool known = false;         // Receiver position only; never implies transmitter location
};

struct SignalMeasurements {
  int16_t relative_level_db_tenths = 0; // Relative, NOT calibrated dBm
  int32_t frequency_offset_hz_tenths = 0;
  uint16_t fec_corrections = 0;
  uint16_t crc_failures = 0;
  bool level_known = false;
  bool offset_known = false;
};

struct Observation {
  Protocol protocol = Protocol::unknown;
  Origin origin = Origin::rf_observation;
  Proof proof = Proof::candidate;
  uint32_t frequency_hz = 0;
  uint64_t utc_ms = 0;
  bool utc_known = false;       // Boot-relative time is not UTC
  ReceiverLocation receiver_location{};
  ProtocolIdentity identity{};
  SignalMeasurements measurements{};
};

constexpr bool is_rf_proven(const Observation& item) {
  return item.origin == Origin::rf_observation &&
         item.proof >= Proof::crc_valid_decode;
}

constexpr bool valid(const Observation& item) {
  if (item.frequency_hz == 0) return false;
  if (item.receiver_location.known &&
      (item.receiver_location.latitude_e7 < -900000000 ||
       item.receiver_location.latitude_e7 > 900000000 ||
       item.receiver_location.longitude_e7 < -1800000000 ||
       item.receiver_location.longitude_e7 > 1800000000)) return false;
  if (item.utc_known && item.utc_ms == 0) return false;
  if ((item.identity.known_fields & ~kKnownFieldMask) != 0) return false;
  // Imported directory and human-entered data never masquerade as decoder evidence.
  if (item.origin != Origin::rf_observation && item.proof != Proof::candidate) return false;
  if (item.protocol == Protocol::unknown &&
      (item.proof >= Proof::crc_valid_decode || item.identity.known_fields != 0)) return false;
  if (item.proof == Proof::site_confirmed &&
      ((item.identity.known_fields & site_known) == 0 ||
       item.origin != Origin::rf_observation)) return false;
  if ((item.identity.known_fields & access_code_known) != 0) {
    if (item.protocol == Protocol::dmr_tier2 && item.identity.access_code > 15) return false;
    if ((item.protocol == Protocol::p25_phase1 || item.protocol == Protocol::p25_phase2) &&
        item.identity.access_code > 4095) return false;
  }
  if ((item.identity.known_fields & slot_known) != 0) {
    if (item.protocol == Protocol::dmr_tier2 &&
        (item.identity.slot < 1 || item.identity.slot > 2)) return false;
    if (item.protocol == Protocol::p25_phase2 && item.identity.slot > 1) return false;
    if (item.protocol != Protocol::dmr_tier2 &&
        item.protocol != Protocol::p25_phase2) return false;
  }
  return true;
}

// Fixed-memory event history, deliberately not a persistent database.
// Storage/import modules should preserve origin and proof without silently promoting them.
template <size_t Capacity> class ObservationRing {
  static_assert(Capacity > 0, "ObservationRing capacity must be positive");
 public:
  bool append(const Observation& item) {
    if (!valid(item)) return false;
    entries_[write_index_] = item;
    write_index_ = (write_index_ + 1) % Capacity;
    if (count_ < Capacity) ++count_;
    return true;
  }
  constexpr size_t size() const { return count_; }
  constexpr size_t capacity() const { return Capacity; }
  const Observation* newest(size_t index = 0) const {
    if (index >= count_) return nullptr;
    return &entries_[(write_index_ + Capacity - 1 - index) % Capacity];
  }
  void clear() { write_index_ = 0; count_ = 0; }
 private:
  Observation entries_[Capacity]{};
  size_t write_index_ = 0;
  size_t count_ = 0;
};

}  // namespace orcsdr::radio_discovery

#pragma once
// Passive Remote ID application-message decoder. NO Bluetooth/Wi-Fi packet acquisition.
// The source adapter must first extract an exact 25-byte ASTM OpenDroneID message
// or a bounded 3+25*N-byte message pack. Do not feed this arbitrary RF bytes.
#include <cstddef>
#include <cstdint>

namespace orcsdr::drone_watch::rid {

constexpr size_t kMessageSize = 25;
constexpr size_t kMaxPackMessages = 9;
constexpr size_t kMaxPayload = 3 + kMaxPackMessages * kMessageSize;

enum class Transport : uint8_t { unknown, bluetooth4, bluetooth5, wifi_beacon, wifi_nan, fixture };
enum class ParseStatus : uint8_t {
  ok, invalid_argument, invalid_length, unsupported_type, unsupported_version, malformed
};
enum class Kind : uint8_t { basic_id, location, auth, self_id, system, operator_id };

struct CaptureMeta {
  Transport transport = Transport::unknown;
  uint32_t received_ms = 0;  // local monotonic; NOT broadcast timestamp or UTC
  uint32_t source_id = 0;    // assigned by a future trusted adapter
  bool test_data = false;    // explicit replay marker independent of RF transport
  bool advertising_counter_present = false; // present in BLE4 ASTM service data
  uint8_t advertising_counter = 0; // NOT a globally unique observation ID
};

struct Observation {
  Kind kind = Kind::basic_id;
  uint8_t protocol_version = 0;
  uint8_t id_type = 0;
  uint8_t aircraft_type = 0;
  char claimed_id[21]{};    // printable string only; binary IDs are not rendered
  bool claimed_id_present = false;
  bool claimed_position_valid = false;
  double reported_latitude = 0.0;
  double reported_longitude = 0.0;
  bool heading_valid = false;
  float reported_heading_degrees = 0.0f;
  bool horizontal_speed_valid = false;
  float reported_speed_mps = 0.0f;
  bool baro_altitude_valid = false;
  float reported_baro_altitude_m = 0.0f;
  bool geo_altitude_valid = false;
  float reported_geo_altitude_m = 0.0f;
  // Auth payload presence does NOT imply a verified signature or identity.
  bool auth_payload_present = false;
};

struct Batch {
  CaptureMeta capture{};
  bool test_data = false;
  uint8_t count = 0;
  Observation messages[kMaxPackMessages]{};
};

// Atomic output: on error, out is zeroed, count is 0 and no partial messages
// are exposed. No dynamic allocation. Preserves source and local receive time.
// Protocol revisions > 2 are rejected rather than guessed compatible.
ParseStatus decode(const uint8_t* payload, size_t length,
                   const CaptureMeta& capture, Batch& out);
}  // namespace orcsdr::drone_watch::rid

#pragma once
// Original, portable AIS message-field parser. Input is an already CRC-validated RF payload.
// No radio, allocation, UI, OrcMaps, or third-party decoder dependency.
#include <cstddef>
#include <cstdint>

namespace orcsdr::ais {

constexpr size_t kMaxMessageBits = 1008;
constexpr size_t kMaxMessageBytes = (kMaxMessageBits + 7) / 8;

enum class ParseStatus : uint8_t { ok, invalid_input, invalid_type, invalid_length };
enum class Coverage : uint8_t {
  header_only,     // type, repeat and source ID only; no field of this type is decoded
  position,        // Types 1-3, 18, 19
  aid21, long_range27, aid28,
  base_station,    // Type 4: fixed position
  static_voyage,   // Type 5: name, call sign, ship type, dimensions, destination
  static_b,        // Type 24 part A (name) or part B (ship type, call sign, dimensions)
  sar_aircraft     // Type 9: position, speed, course
};

// Static identity fields (Types 5, 19 and 24). A field is present only when its has_ flag is set:
// an all-'@' string or a zero ship type means "not available" on the air and is never shown as a value.
struct VesselStatic {
  char name[21]{}, callsign[8]{}, destination[21]{};
  bool has_name = false, has_callsign = false, has_destination = false;
  bool has_ship_type = false, has_dimensions = false;
  uint8_t ship_type = 0;
  uint32_t imo = 0;                 // Type 5 only; 0 = not available
  uint16_t to_bow_m = 0, to_stern_m = 0;
  uint8_t to_port_m = 0, to_starboard_m = 0;
  uint8_t part = 0;                 // Type 24: 0 = part A, 1 = part B
};

struct Aid {
  // Type 21: the name is decoded; Type 28 carries no name.
  char name[37]{};
  bool is_virtual = false;   // Only valid for Type 21.
  bool virtual_flag_valid = false;
  uint8_t aid_type = 0;     // Type 21: 5-bit AtoN type; Type 28: 7-bit AtoN type.
  uint8_t station_type = 0; // Type 28 3-bit station type, uninterpreted here.
  uint8_t dimension_type = 0; // Type 28: preserve type-dependent geometry, do not invent polygon coordinates.
  uint16_t dimension_a = 0, dimension_b = 0;
  uint32_t iala_mrn = 0;
  uint8_t restricted_use = 0, timestamp = 0, on_station = 0, status = 0;
  bool additional_data = false, charted = false, authenticated = false;
};

struct Message {
  uint8_t type = 0, repeat = 0;
  uint32_t source_id = 0; // MMSI for normal reports; Source ID for Type 28.
  size_t bits = 0;
  Coverage coverage = Coverage::header_only;
  bool has_position = false;
  int32_t latitude_e7 = 0, longitude_e7 = 0;
  bool has_speed = false, has_course = false, has_heading = false;
  uint16_t speed_tenths_kn = 0, course_tenths_deg = 0, heading_deg = 0;
  bool has_nav_status = false;      // Types 1-3; 15 (not defined) is reported as absent
  uint8_t nav_status = 0;
  Aid aid{};
  VesselStatic vessel{};
};

struct TypeLength { uint16_t minimum, maximum; };
TypeLength type_length(uint8_t type);
// Bit 0 is the MSB of bytes[0]. The caller must validate RF CRC/FCS before invoking parse().
ParseStatus parse(const uint8_t* bytes, size_t bit_count, Message* result);

} // namespace orcsdr::ais

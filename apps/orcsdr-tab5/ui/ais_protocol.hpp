#pragma once
// Original, portable AIS message-field parser. Input is an already CRC-validated RF payload.
// No radio, allocation, UI, OrcMaps, or third-party decoder dependency.
#include <cstddef>
#include <cstdint>

namespace orcsdr::ais {

constexpr size_t kMaxMessageBits = 1008;
constexpr size_t kMaxMessageBytes = (kMaxMessageBits + 7) / 8;

enum class ParseStatus : uint8_t { ok, invalid_input, invalid_type, invalid_length };
enum class Coverage : uint8_t { header_only, position, aid21, long_range27, aid28 };

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
  Aid aid{};
};

struct TypeLength { uint16_t minimum, maximum; };
TypeLength type_length(uint8_t type);
// Bit 0 is the MSB of bytes[0]. The caller must validate RF CRC/FCS before invoking parse().
ParseStatus parse(const uint8_t* bytes, size_t bit_count, Message* result);

} // namespace orcsdr::ais

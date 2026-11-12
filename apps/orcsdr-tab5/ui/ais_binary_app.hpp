#pragma once
// Portable, bounded AIS binary application decoder.
// Supports message 6, 8, 25, 26 envelopes, and selected IALA-defined DAC/FI.
// No RF receiver, graphics, platform headers, heap allocations, or vendor code.
#include <cstddef>
#include <cstdint>

namespace orcsdr::ais::app {
constexpr size_t kMaxBits=1008;
constexpr size_t kMaxBytes=(kMaxBits+7)/8;

enum class Status : uint8_t {
  decoded, preserved_unknown, unstructured, not_binary, invalid_input, truncated
};
enum class Kind : uint8_t {
  unknown, imo_persons, inland_persons, inland_vessel
};
struct Descriptor {
  uint8_t message_type;
  uint16_t dac;
  uint8_t fi;
  Kind kind;
  const char* name;
  const char* specification;
};
size_t descriptor_count();
const Descriptor* descriptor_at(size_t i);
const Descriptor* find(uint8_t message_type,uint16_t dac,uint8_t fi);

struct Result {
  Status status=Status::invalid_input;
  Kind kind=Kind::unknown;
  uint8_t message_type=0;
  uint32_t source_id=0,destination_id=0;
  bool addressed=false,has_destination=false,has_application_id=false;
  uint16_t dac=0;
  uint8_t fi=0;
  // All application bits after the 16-bit application identifier, including
  // unknown applications, preserved exactly MSB-first (low unused bits are 0).
  uint16_t app_bits=0;
  uint8_t app_raw[kMaxBytes]{};
  // IMO DAC 1 / FI 16. 0 = not reported; 8191 = 8191 or more.
  uint16_t persons=0;
  bool persons_known=false,persons_at_least_8191=false;
  // EU inland DAC 200 / FI 55; unknown crew/personnel=255, passenger=8191.
  uint16_t crew=0,passengers=0,personnel=0;
  bool crew_known=false,passengers_known=false,personnel_known=false;
  // EU inland DAC 200 / FI 10. Units retained (length/beam in 0.1m,
  // draught in 0.01m), unknown not substituted with 0.
  char eni[9]{};
  uint16_t length_dm=0,beam_dm=0,ship_type=0,draught_cm=0;
  bool length_known=false,beam_known=false,draught_known=false;
  uint8_t hazardous_cargo=0,loaded=0,quality_flags=0;
};
// Caller must already have verified RF CRC or NMEA sentence checksum.
// This function never claims that unrecognized DAC/FI fields were decoded.
Status decode(const uint8_t* payload,size_t bits,Result* result);
} // namespace orcsdr::ais::app

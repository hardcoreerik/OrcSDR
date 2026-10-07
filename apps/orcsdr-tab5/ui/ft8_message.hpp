#pragma once

#include "ft8_codec.hpp"

#include <cstdint>

namespace orcsdr::ft8::message {

enum class CallsignKind : uint8_t {
  standard,
  cq,
  de,
  qrz,
  hash22,
  unsupported,
};

struct Callsign {
  CallsignKind kind = CallsignKind::unsupported;
  char text[16]{};
  uint32_t hash22 = 0;
};

enum class ExtraKind : uint8_t {
  grid,
  report,
  blank,
  rrr,
  rr73,
  seventy_three,
  unsupported,
};

struct Extra {
  ExtraKind kind = ExtraKind::unsupported;
  char text[8]{};
  int16_t report_db = 0;
};

struct StandardMessage {
  uint8_t type = 0;  // i3: 1 = /R family, 2 = /P family
  Callsign first{};
  Callsign second{};
  bool first_suffix = false;
  bool second_suffix = false;
  bool roger = false;
  Extra extra{};
  bool fully_renderable = false;
  char text[48]{};
};

// Unpacks the overwhelmingly common FT8/FT4 standard message families:
// i3=1 (optional /R suffix) and i3=2 (optional /P suffix).
//
// The parser is deliberately conservative. Unknown CQ modifiers or any field
// whose meaning is not independently implemented return false instead of being
// guessed. 22-bit hashes are recognized structurally but leave
// fully_renderable=false until a separate callsign cache resolves them.
bool unpack_standard(const codec::PayloadBits& payload, StandardMessage* out);

// Decodes only the protocol-defined c28 field itself. This recognizes
// DE/QRZ/CQ, 22-bit hashes, and canonical standard callsigns.
bool decode_c28(uint32_t value, Callsign* out);

// Decodes g15 into grid/report/acknowledgement/blank.
bool decode_g15(uint16_t value, Extra* out);

bool self_check();

}  // namespace orcsdr::ft8::message

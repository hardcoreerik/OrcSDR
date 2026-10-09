#pragma once

#include "js8_codec.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::message {

// Rendering of CRC-valid JS8 frames into text. Only what has been verified against real signals is rendered; every other frame kind is reported as not
// rendered (it is still a valid radio frame, never an invented message). Verified so far (docs/js8/spec/js8_message_formats.json, real capture
// sample-40m-180s-002): the directed frame (kind 3) carrying command 29 "HEARTBEAT SNR" with a numeric SNR.
//
// Directed frame, 72 text bits: kind(3) | source callsign(28) | destination callsign(28) | command(5) | extra(8).
// A 28-bit base callsign is unpacked right to left: three suffix characters in radix 27 (A-Z and blank), one digit in radix 10, a second prefix
// character in radix 36, and the remaining quotient is the first prefix character. Callsigns with '/' or '@' (compound or group calls) are not rendered.
constexpr uint8_t kKindDirected = 3;
constexpr uint8_t kFlagsFirstAndLast = 3;
constexpr uint8_t kCommandHeartbeatSnr = 29;

struct Directed {
  char source[12]{};
  char destination[12]{};
  uint8_t command = 0;
  uint8_t extra = 0;
  bool snr_present = false;
  int8_t snr_db = 0;       // the SNR the sender reported in the payload (not a measurement made by this receiver)
};

// Unpacks one 28-bit base callsign. Returns false when the value is outside the callsign space or the result is not a plausible callsign.
bool unpack_callsign(uint32_t value, char out[12]);

// Reads the directed-frame fields from the 72 payload bits (no rendering rules applied).
bool unpack_directed(const uint8_t payload[codec::kTextBits], Directed* out);

struct Rendered {
  bool ok = false;
  char text[48]{};         // "WO7I: ND7M HEARTBEAT SNR +11"
  char source[12]{};
  Directed directed{};
};

// Renders a frame if (and only if) it is a verified, plausible message. The caller has already required parity and CRC to pass.
bool render(const codec::Fields& fields, Rendered* out);

bool self_check();

}  // namespace orcsdr::js8::message

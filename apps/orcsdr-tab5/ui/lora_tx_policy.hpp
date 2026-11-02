#pragma once
// TX admission policy shared by LoRa dashboards and future SX1262 transports.
// A board profile is NOT proof that an RF device is present or safe to transmit.
// No hardware writes or RF generation live in this file.
#include <cstddef>
#include <cstdint>

namespace orcsdr::lora_tx {

enum class Readiness : uint8_t {
  absent,        // no supported radio in the active board profile
  profile_only,  // pins documented, chip not probed
  detected,      // chip has answered, RF path unverified
  rx_verified,   // packet receive independently exercised
  tx_verified,   // conducted RF transmit independently exercised
};

enum class Denial : uint8_t {
  none,
  radio_unverified,
  antenna_unverified,
  region_unapproved,
  frequency_unapproved,
  not_armed,
  busy,
  invalid_payload,
};

struct Capability {
  Readiness readiness = Readiness::absent;
  bool antenna_path_verified = false;
  bool regional_tx_profile_approved = false;
  bool frequency_approved = false;
  bool operator_armed = false;
  bool busy = false;
  uint32_t frequency_hz = 0;
  uint8_t max_payload_bytes = 0;
};

// All booleans are FALSE by default and must be independently established.
// An RX frequency selection does not constitute TX frequency approval.
constexpr Denial can_arm(const Capability& c) {
  if (c.readiness != Readiness::tx_verified) return Denial::radio_unverified;
  if (!c.antenna_path_verified) return Denial::antenna_unverified;
  if (!c.regional_tx_profile_approved) return Denial::region_unapproved;
  if (!c.frequency_approved || c.frequency_hz == 0) return Denial::frequency_unapproved;
  if (c.busy) return Denial::busy;
  return Denial::none;
}

constexpr Denial can_send(const Capability& c, size_t payload_bytes) {
  const Denial gate = can_arm(c);
  if (gate != Denial::none) return gate;
  if (!c.operator_armed) return Denial::not_armed;
  if (payload_bytes == 0 || c.max_payload_bytes == 0 ||
      payload_bytes > c.max_payload_bytes) return Denial::invalid_payload;
  return Denial::none;
}

constexpr const char* readiness_name(Readiness r) {
  switch (r) {
    case Readiness::absent: return "NO RADIO";
    case Readiness::profile_only: return "PROFILE ONLY";
    case Readiness::detected: return "DETECTED / RF UNVERIFIED";
    case Readiness::rx_verified: return "RX VERIFIED / TX LOCKED";
    case Readiness::tx_verified: return "CONDUCTED TX VERIFIED";
  }
  return "UNKNOWN";
}

constexpr const char* denial_name(Denial d) {
  switch (d) {
    case Denial::none: return "READY";
    case Denial::radio_unverified: return "TX NOT BENCH VERIFIED";
    case Denial::antenna_unverified: return "ANTENNA PATH UNVERIFIED";
    case Denial::region_unapproved: return "REGIONAL TX PROFILE NOT APPROVED";
    case Denial::frequency_unapproved: return "TX FREQUENCY NOT APPROVED";
    case Denial::not_armed: return "OPERATOR ARM REQUIRED";
    case Denial::busy: return "RADIO BUSY";
    case Denial::invalid_payload: return "INVALID PACKET";
  }
  return "UNKNOWN";
}

}  // namespace orcsdr::lora_tx

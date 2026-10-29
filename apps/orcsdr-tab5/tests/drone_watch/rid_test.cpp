// Host-only conformance and defensive-boundary test for the exact
// vendored upstream Apache-2.0 OpenDroneID implementation.
// All fixtures are synthetic/hand-constructed. No RF acquisition is proven.
#include "drone_watch_rid.hpp"
#include "third_party/opendroneid/opendroneid.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>

using namespace orcsdr::drone_watch::rid;

static void near(double actual, double expected, double tolerance = 1e-5) {
  assert(std::fabs(actual - expected) <= tolerance);
}
static std::array<uint8_t, 25> basic() {
  std::array<uint8_t, 25> raw{};
  raw[0] = 0x02; // Basic ID, protocol version 2
  raw[1] = 0x12; // ID type Serial 1, UA type multirotor 2
  const char* id = "ORC-TEST-001";
  std::memcpy(raw.data() + 2, id, std::strlen(id));
  return raw;
}
static std::array<uint8_t, 25> location() {
  // Independent little-endian byte fixture, not created with upstream encode.
  std::array<uint8_t, 25> raw{};
  raw[0] = 0x12; // Location, protocol 2
  raw[1] = 0x10; // Ground status
  raw[2] = 90;   // 90 degrees east
  raw[3] = 20;   // 5.0 m/s, scaling 0.25
  const int32_t lat = 440521000;   // 44.0521 degrees
  const int32_t lon = -1230867000; // -123.0867 degrees
  for (int i = 0; i < 4; ++i) {
    raw[5 + i] = static_cast<uint8_t>(static_cast<uint32_t>(lat) >> (8 * i));
    raw[9 + i] = static_cast<uint8_t>(static_cast<uint32_t>(lon) >> (8 * i));
  }
  raw[15] = 0xB6; raw[16] = 0x08; // (115m + 1000m) * 2 = 2230
  raw[21] = 0xFF; raw[22] = 0xFF; // unknown timestamp
  return raw;
}
int main() {
  static_assert(ODID_MESSAGE_SIZE == kMessageSize, "wire size drift");
  static_assert(ODID_PACK_MAX_MESSAGES == kMaxPackMessages, "pack size drift");
  static_assert(sizeof(ODID_Message_encoded) == kMessageSize, "vendor wire layout drift");

  Batch result{};
  CaptureMeta hw{Transport::bluetooth4, 8000, 17};
  auto id = basic();
  assert(decode(id.data(), id.size(), hw, result) == ParseStatus::ok);
  assert(result.count == 1 && !result.test_data);
  assert(result.capture.source_id == 17 && result.capture.received_ms == 8000);
  assert(result.messages[0].kind == Kind::basic_id);
  assert(result.messages[0].protocol_version == 2);
  assert(result.messages[0].claimed_id_present);
  assert(std::strcmp(result.messages[0].claimed_id, "ORC-TEST-001") == 0);
  assert(result.messages[0].aircraft_type == 2);
  assert(!result.messages[0].claimed_position_valid);

  auto loc = location();
  assert(decode(loc.data(), loc.size(), hw, result) == ParseStatus::ok);
  assert(result.count == 1 && result.messages[0].kind == Kind::location);
  assert(result.messages[0].claimed_position_valid);
  near(result.messages[0].reported_latitude, 44.0521);
  near(result.messages[0].reported_longitude, -123.0867);
  assert(result.messages[0].heading_valid);
  near(result.messages[0].reported_heading_degrees, 90);
  assert(result.messages[0].horizontal_speed_valid);
  near(result.messages[0].reported_speed_mps, 5);
  assert(!result.messages[0].baro_altitude_valid);
  assert(result.messages[0].geo_altitude_valid);
  near(result.messages[0].reported_geo_altitude_m, 115);

  // Differential check: upstream reference encoder and our own decoder agree
  // on the exact source fields; test vectors above are independently supplied.
  ODID_BasicID_data upstream_id{};
  odid_initBasicIDData(&upstream_id);
  upstream_id.IDType = ODID_IDTYPE_SERIAL_NUMBER;
  upstream_id.UAType = ODID_UATYPE_HELICOPTER_OR_MULTIROTOR;
  std::memcpy(upstream_id.UASID, "ORC-TEST-001", 12);
  ODID_BasicID_encoded ref{};
  assert(encodeBasicIDMessage(&ref, &upstream_id) == ODID_SUCCESS);
  assert(std::memcmp(&ref, id.data(), kMessageSize) == 0);

  // Explicit application-level message pack, 3 header bytes + 2 messages.
  std::array<uint8_t, 3 + 2 * kMessageSize> pack{};
  pack[0] = 0xF2; pack[1] = 25; pack[2] = 2;
  std::memcpy(pack.data() + 3, id.data(), 25);
  std::memcpy(pack.data() + 28, loc.data(), 25);
  CaptureMeta replay{Transport::fixture, 1234, 9};
  assert(decode(pack.data(), pack.size(), replay, result) == ParseStatus::ok);
  assert(result.count == 2 && result.test_data);
  assert(result.messages[0].kind == Kind::basic_id);
  assert(result.messages[1].claimed_position_valid);
  assert(result.capture.received_ms == 1234);

  assert(decode(nullptr, 25, hw, result) == ParseStatus::invalid_argument);
  assert(result.count == 0);
  assert(decode(id.data(), 24, hw, result) == ParseStatus::invalid_length);
  assert(decode(id.data(), 26, hw, result) == ParseStatus::invalid_length);
  assert(result.count == 0);
  auto bad = id;
  bad[0] = 0x03; // Known message type, future protocol version 3
  assert(decode(bad.data(), bad.size(), hw, result) == ParseStatus::unsupported_version);
  bad[0] = 0xE2; // unknown message family
  assert(decode(bad.data(), bad.size(), hw, result) == ParseStatus::unsupported_type);
  bad = id;
  bad[1] = 0x12; bad[2] = 0x01; // non-printable serial must not render as text
  assert(decode(bad.data(), bad.size(), hw, result) == ParseStatus::ok);
  assert(!result.messages[0].claimed_id_present);

  auto unknown = loc;
  for (int i = 5; i <= 12; ++i) unknown[i] = 0;
  unknown[3] = 0xFF; // unknown speed
  assert(decode(unknown.data(), unknown.size(), hw, result) == ParseStatus::ok);
  assert(!result.messages[0].claimed_position_valid);
  assert(!result.messages[0].horizontal_speed_valid);
  unknown = loc;
  const int32_t invalid_lat = 910000000; // 91.0 degrees
  for (int i = 0; i < 4; ++i)
    unknown[5 + i] = static_cast<uint8_t>(static_cast<uint32_t>(invalid_lat) >> (8 * i));
  assert(decode(unknown.data(), unknown.size(), hw, result) == ParseStatus::ok);
  assert(!result.messages[0].claimed_position_valid);

  pack[1] = 24;
  assert(decode(pack.data(), pack.size(), hw, result) == ParseStatus::invalid_length);
  assert(result.count == 0); // no partially visible valid observations
  pack[1] = 25; pack[2] = 0;
  assert(decode(pack.data(), pack.size(), hw, result) == ParseStatus::invalid_length);
  pack[2] = 10;
  assert(decode(pack.data(), pack.size(), hw, result) == ParseStatus::invalid_length);
  pack[2] = 2;
  pack[28] = 0xF2; // nested pack rejected, even though first message good
  assert(decode(pack.data(), pack.size(), hw, result) == ParseStatus::unsupported_type);
  assert(result.count == 0);
  pack[28] = loc[0];

  std::array<uint8_t, kMaxPayload + 1> oversized{};
  oversized[0] = 0xF2;
  assert(decode(oversized.data(), oversized.size(), hw, result) == ParseStatus::invalid_length);

  // Parser must never read beyond an arbitrary supplied payload or expose
  // output from an incomplete/invalid frame. No synthetic live claims.
  std::mt19937 rng(0x0D1D);
  std::array<uint8_t, kMaxPayload> fuzz{};
  for (int round = 0; round < 5000; ++round) {
    size_t length = static_cast<size_t>(rng() % (kMaxPayload + 1));
    for (size_t i = 0; i < length; ++i) fuzz[i] = static_cast<uint8_t>(rng());
    const auto status = decode(fuzz.data(), length, hw, result);
    if (status != ParseStatus::ok) assert(result.count == 0);
    else assert(result.count >= 1 && result.count <= kMaxPackMessages);
  }
  std::cout << "Drone Watch RID P1 PASS: reference core, explicit wire vectors, pack bounds, 5000 fuzz cases\n";
}

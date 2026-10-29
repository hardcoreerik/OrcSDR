#include "drone_watch_rid.hpp"

#include <cmath>
#include <cstring>
#include "third_party/opendroneid/opendroneid.h"

namespace orcsdr::drone_watch::rid {
namespace {

ParseStatus decode_one(const uint8_t* bytes, Observation& output) {
  if (bytes == nullptr) return ParseStatus::invalid_argument;
  const uint8_t kind = static_cast<uint8_t>(bytes[0] >> 4);
  const uint8_t version = static_cast<uint8_t>(bytes[0] & 0x0f);
  if (version > ODID_PROTOCOL_VERSION) return ParseStatus::unsupported_version;
  if (kind > ODID_MESSAGETYPE_OPERATOR_ID) return ParseStatus::unsupported_type;

  // Do not reinterpret an unaligned receive buffer as bitfield structs.
  ODID_Message_encoded encoded{};
  std::memcpy(encoded.rawData, bytes, ODID_MESSAGE_SIZE);
  Observation msg{};
  msg.protocol_version = version;
  int result = ODID_FAIL;
  switch (kind) {
    case ODID_MESSAGETYPE_BASIC_ID: {
      ODID_BasicID_data id{};
      result = decodeBasicIDMessage(&id, &encoded.basicId);
      msg.kind = Kind::basic_id;
      if (result == ODID_SUCCESS) {
        msg.id_type = static_cast<uint8_t>(id.IDType);
        msg.aircraft_type = static_cast<uint8_t>(id.UAType);
        // Binary session IDs cannot be safely shown as a plain text serial.
        if (msg.id_type == ODID_IDTYPE_SERIAL_NUMBER ||
            msg.id_type == ODID_IDTYPE_CAA_REGISTRATION_ID) {
          bool safe = id.UASID[0] != '\0';
          size_t len = 0;
          for (; len < ODID_ID_SIZE && id.UASID[len] != '\0'; ++len) {
            const uint8_t c = static_cast<uint8_t>(id.UASID[len]);
            if (c < 0x20 || c > 0x7e) safe = false;
          }
          if (safe) {
            std::memcpy(msg.claimed_id, id.UASID, len);
            msg.claimed_id[len] = '\0';
            msg.claimed_id_present = true;
          }
        }
      }
      break;
    }
    case ODID_MESSAGETYPE_LOCATION: {
      ODID_Location_data loc{};
      result = decodeLocationMessage(&loc, &encoded.location);
      msg.kind = Kind::location;
      if (result == ODID_SUCCESS) {
        // OpenDroneID defines a (0,0) latitude/longitude pair as missing.
        const bool range = std::isfinite(loc.Latitude) && std::isfinite(loc.Longitude) &&
          loc.Latitude >= -90.0 && loc.Latitude <= 90.0 &&
          loc.Longitude >= -180.0 && loc.Longitude <= 180.0;
        msg.claimed_position_valid = range && (loc.Latitude != 0.0 || loc.Longitude != 0.0);
        if (msg.claimed_position_valid) {
          msg.reported_latitude = loc.Latitude;
          msg.reported_longitude = loc.Longitude;
        }
        msg.heading_valid = std::isfinite(loc.Direction) && loc.Direction >= 0 &&
                             loc.Direction <= 360;
        if (msg.heading_valid) msg.reported_heading_degrees = loc.Direction;
        msg.horizontal_speed_valid = std::isfinite(loc.SpeedHorizontal) &&
           loc.SpeedHorizontal >= 0 && loc.SpeedHorizontal <= MAX_SPEED_H;
        if (msg.horizontal_speed_valid) msg.reported_speed_mps = loc.SpeedHorizontal;
        msg.baro_altitude_valid = std::isfinite(loc.AltitudeBaro) && loc.AltitudeBaro > INV_ALT;
        if (msg.baro_altitude_valid) msg.reported_baro_altitude_m = loc.AltitudeBaro;
        msg.geo_altitude_valid = std::isfinite(loc.AltitudeGeo) && loc.AltitudeGeo > INV_ALT;
        if (msg.geo_altitude_valid) msg.reported_geo_altitude_m = loc.AltitudeGeo;
      }
      break;
    }
    case ODID_MESSAGETYPE_AUTH: {
      ODID_Auth_data auth{};
      result = decodeAuthMessage(&auth, &encoded.auth);
      msg.kind = Kind::auth;
      msg.auth_payload_present = result == ODID_SUCCESS;
      break;
    }
    case ODID_MESSAGETYPE_SELF_ID: {
      ODID_SelfID_data self{};
      result = decodeSelfIDMessage(&self, &encoded.selfId);
      msg.kind = Kind::self_id;
      break;
    }
    case ODID_MESSAGETYPE_SYSTEM: {
      ODID_System_data system{};
      result = decodeSystemMessage(&system, &encoded.system);
      msg.kind = Kind::system;
      // Operator location is intentionally NOT copied into our display model.
      break;
    }
    case ODID_MESSAGETYPE_OPERATOR_ID: {
      ODID_OperatorID_data operator_id{};
      result = decodeOperatorIDMessage(&operator_id, &encoded.operatorId);
      msg.kind = Kind::operator_id;
      // Operator identity is intentionally NOT copied into our display model.
      break;
    }
    default: return ParseStatus::unsupported_type;
  }
  if (result != ODID_SUCCESS) return ParseStatus::malformed;
  output = msg;
  return ParseStatus::ok;
}
}  // namespace

ParseStatus decode(const uint8_t* payload, size_t length,
                   const CaptureMeta& capture, Batch& out) {
  out = {};
  if (!payload) return ParseStatus::invalid_argument;
  if (length < kMessageSize || length > kMaxPayload) return ParseStatus::invalid_length;
  uint8_t count = 1;
  const uint8_t* messages = payload;
  if ((payload[0] >> 4) == ODID_MESSAGETYPE_PACKED) {
    if ((payload[0] & 0x0f) > ODID_PROTOCOL_VERSION) return ParseStatus::unsupported_version;
    if (length < 3 || payload[1] != kMessageSize) return ParseStatus::invalid_length;
    count = payload[2];
    if (count < 1 || count > kMaxPackMessages ||
        length != 3u + static_cast<size_t>(count) * kMessageSize)
      return ParseStatus::invalid_length;
    messages = payload + 3;
  } else if (length != kMessageSize) {
    return ParseStatus::invalid_length;
  }

  Batch staged{};
  staged.capture = capture;
  staged.test_data = capture.transport == Transport::fixture;
  for (uint8_t i = 0; i < count; ++i) {
    const ParseStatus result = decode_one(messages + static_cast<size_t>(i) * kMessageSize,
                                          staged.messages[i]);
    if (result != ParseStatus::ok) return result;
  }
  staged.count = count;
  out = staged;
  return ParseStatus::ok;
}
}  // namespace orcsdr::drone_watch::rid

#include "drone_watch_ble4.hpp"
#include <cstring>
namespace orcsdr::drone_watch::rid {
Ble4FrameStatus extract_ble4_advertising_data(const uint8_t* ad, size_t length,
                                               Ble4Message& message) {
  message = Ble4Message{};
  if (!ad && length != 0) return Ble4FrameStatus::invalid_argument;
  if (length > 31) return Ble4FrameStatus::invalid_length;
  if (length == 0) return Ble4FrameStatus::no_remote_id;
  Ble4Message staged{};
  bool found = false;
  for (size_t pos = 0; pos < length;) {
    const uint8_t field_len = ad[pos++]; // bytes after length, including type
    if (!field_len) break;              // zero-length AD terminator
    if (static_cast<size_t>(field_len) > length - pos)
      return Ble4FrameStatus::malformed_ad;
    const uint8_t type = ad[pos];
    // 0x16: Service Data - 16-bit UUID; 0xFFFA in little endian.
    if (type == 0x16 && field_len >= 3 &&
        ad[pos + 1] == 0xFA && ad[pos + 2] == 0xFF) {
      // UUID is ASTM; 0x0D is the Open Drone ID application code.
      if (field_len < 4) return Ble4FrameStatus::malformed_remote_id;
      if (ad[pos + 3] == 0x0D) {
        // [type, UUID0, UUID1, app, counter, 25-byte ODID] = 30 bytes.
        if (field_len != 30 || found)
          return Ble4FrameStatus::malformed_remote_id;
        staged.advertising_counter = ad[pos + 4];
        std::memcpy(staged.encoded, ad + pos + 5, kMessageSize);
        found = true;
      }
    }
    pos += field_len;
  }
  if (!found) return Ble4FrameStatus::no_remote_id;
  message = staged;
  return Ble4FrameStatus::matched;
}
}

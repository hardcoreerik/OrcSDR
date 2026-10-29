#pragma once
// Decode the Bluetooth 4 *advertising-data TLV*, not the HCI event or RF PDU.
// Upstream opendroneid/transmitter-linux bluetooth.c sends:
// 1E 16 FA FF 0D <counter> <25-byte OpenDroneID message>.
// Does not scan or connect to Bluetooth and does not attribute aircraft identity.
#include <cstddef>
#include <cstdint>
#include "drone_watch_rid.hpp"
namespace orcsdr::drone_watch::rid {
enum class Ble4FrameStatus : uint8_t {
  matched, no_remote_id, invalid_argument, invalid_length, malformed_ad,
  malformed_remote_id
};
struct Ble4Message {
  uint8_t advertising_counter = 0;
  uint8_t encoded[kMessageSize]{};
};
// Length excludes the HCI Advertising_Data_Length byte; max legacy AD = 31.
// On any failure, message is cleared. All bytes and lengths are bounded.
// A successful extraction is NOT a successful Remote ID *decode* or proof of RF.
Ble4FrameStatus extract_ble4_advertising_data(const uint8_t* ad, size_t length,
                                               Ble4Message& message);
}

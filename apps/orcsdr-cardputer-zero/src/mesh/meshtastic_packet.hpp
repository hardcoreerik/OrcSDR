#pragma once
// Meshtastic application layer, for frames the native LoRa decoder has already
// validated (CRC-ok payloads). This does NOT decode LoRa: symbols, whitening and
// CRC stay in lora_native_decoder, which is shared with the Tab5 build.
//
// What lives here is everything above the PHY:
//   * the 16-byte MeshPacket header (to/from/id/flags/channel/next_hop/relay)
//   * AES-128-CTR decryption with the channel key
//   * a minimal protobuf reader for the Data message (portnum + payload)
//
// Written from the published protocol layout. No third-party source is copied;
// the protobuf field numbers and the header layout are public interface facts,
// and AES comes from mbedcrypto, which the LoRa decoder already links.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orc::zero::mesh::meshtastic {

// Default primary-channel key: Meshtastic stores "AQ==" (a single byte 0x01)
// and expands it to this well-known 16-byte value.
inline constexpr uint8_t kDefaultKey[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                            0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

struct Header {
  uint32_t to = 0;
  uint32_t from = 0;
  uint32_t id = 0;
  uint8_t flags = 0;
  uint8_t channel = 0;
  uint8_t next_hop = 0;
  uint8_t relay_node = 0;

  // Hop limit and hop start share `flags`: low three bits are the remaining
  // hops, the top nibble records what the sender started with.
  uint8_t hop_limit() const { return uint8_t(flags & 0x07); }
  uint8_t hop_start() const { return uint8_t((flags >> 4) & 0x0f); }
  bool broadcast() const { return to == 0xFFFFFFFFu; }
};

struct Data {
  uint32_t portnum = 0;
  std::vector<uint8_t> payload;
};

// Decoded POSITION_APP payload. Coordinates arrive as degrees * 1e7 in a
// signed fixed32, which is why they must be read as wire type 5 and cast to
// signed before scaling.
struct Position {
  double latitude = 0, longitude = 0;
  int32_t altitude = 0;
  uint32_t time = 0;
  bool has_altitude = false, has_time = false;
};

// Port numbers used by the UI. Values are the published Meshtastic enum.
enum Portnum : uint32_t {
  kTextMessageApp = 1,
  kPositionApp = 3,
  kNodeInfoApp = 4,
  kRoutingApp = 5,
  kTelemetryApp = 67,
};

constexpr size_t kHeaderBytes = 16;

bool parse_header(const uint8_t* frame, size_t size, Header& header);

// AES-128-CTR. The nonce is the packet id then the sender address, both
// little-endian, followed by four zero bytes; the same block counter then covers
// the payload.
bool decrypt(const Header& header, const std::vector<uint8_t>& ciphertext, const uint8_t* key,
             std::vector<uint8_t>& plaintext);

// Minimal protobuf reader for `Data`: field 1 = portnum (varint), field 2 =
// payload (bytes). Unknown fields are skipped rather than rejected, so a newer
// sender still yields the parts we understand.
bool parse_data(const std::vector<uint8_t>& bytes, Data& data);

// Convenience: printable text of a TEXT_MESSAGE_APP payload, control bytes
// replaced. Returns false when the payload is not text.
bool text_of(const Data& data, std::string& text);

// POSITION_APP payload. Returns false when lat/lon are absent or out of range.
bool parse_position(const Data& data, Position& position);

const char* port_name(uint32_t portnum);

}  // namespace orc::zero::mesh::meshtastic

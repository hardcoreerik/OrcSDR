#include "meshtastic_packet.hpp"

#include <cstring>

#include <mbedtls/aes.h>

namespace orc::zero::mesh::meshtastic {
namespace {

uint32_t read_le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Protobuf varint, at most 10 bytes.
bool read_varint(const std::vector<uint8_t>& bytes, size_t& pos, uint64_t& value) {
  value = 0;
  int shift = 0;
  while (pos < bytes.size() && shift < 64) {
    const uint8_t byte = bytes[pos++];
    value |= uint64_t(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) return true;
    shift += 7;
  }
  return false;
}

}  // namespace

bool parse_header(const uint8_t* frame, size_t size, Header& header) {
  if (!frame || size < kHeaderBytes) return false;
  header.to = read_le32(frame + 0);
  header.from = read_le32(frame + 4);
  header.id = read_le32(frame + 8);
  header.flags = frame[12];
  header.channel = frame[13];
  header.next_hop = frame[14];
  header.relay_node = frame[15];
  return true;
}

bool decrypt(const Header& header, const std::vector<uint8_t>& ciphertext, const uint8_t* key,
             std::vector<uint8_t>& plaintext) {
  if (!key || ciphertext.empty()) return false;
  plaintext.assign(ciphertext.size(), 0);

  // Nonce layout, little-endian: the packet id as a 64-bit value, then the
  // sender address, then four zero bytes. The header only carries 32 bits of id,
  // so the upper half stays zero - putting the address at byte 4 instead of 8
  // produces a valid-looking stream that never decrypts.
  uint8_t nonce[16] = {0};
  nonce[0] = uint8_t(header.id);
  nonce[1] = uint8_t(header.id >> 8);
  nonce[2] = uint8_t(header.id >> 16);
  nonce[3] = uint8_t(header.id >> 24);
  nonce[8] = uint8_t(header.from);
  nonce[9] = uint8_t(header.from >> 8);
  nonce[10] = uint8_t(header.from >> 16);
  nonce[11] = uint8_t(header.from >> 24);

  mbedtls_aes_context context;
  mbedtls_aes_init(&context);
  if (mbedtls_aes_setkey_enc(&context, key, 128) != 0) {
    mbedtls_aes_free(&context);
    return false;
  }
  uint8_t stream_block[16] = {0};
  size_t offset = 0;
  const int rc = mbedtls_aes_crypt_ctr(&context, plaintext.size(), &offset, nonce, stream_block,
                                       ciphertext.data(), plaintext.data());
  mbedtls_aes_free(&context);
  return rc == 0;
}

bool parse_data(const std::vector<uint8_t>& bytes, Data& data) {
  size_t pos = 0;
  bool saw_portnum = false;
  while (pos < bytes.size()) {
    uint64_t tag = 0;
    if (!read_varint(bytes, pos, tag)) return false;
    const uint32_t field = uint32_t(tag >> 3);
    const uint32_t wire = uint32_t(tag & 0x07);
    if (wire == 0) {  // varint
      uint64_t value = 0;
      if (!read_varint(bytes, pos, value)) return false;
      if (field == 1) {
        data.portnum = uint32_t(value);
        saw_portnum = true;
      }
    } else if (wire == 2) {  // length-delimited
      uint64_t length = 0;
      if (!read_varint(bytes, pos, length)) return false;
      if (length > bytes.size() - pos) return false;
      if (field == 2)
        data.payload.assign(bytes.begin() + pos, bytes.begin() + pos + size_t(length));
      pos += size_t(length);
    } else if (wire == 5) {  // fixed32
      if (pos + 4 > bytes.size()) return false;
      pos += 4;
    } else if (wire == 1) {  // fixed64
      if (pos + 8 > bytes.size()) return false;
      pos += 8;
    } else {
      return false;  // groups are not used by this protocol
    }
  }
  // A Data without a portnum is not usable; unknown fields alone are tolerated.
  return saw_portnum;
}

bool text_of(const Data& data, std::string& text) {
  if (data.portnum != kTextMessageApp || data.payload.empty()) return false;
  text.assign(data.payload.begin(), data.payload.end());
  for (char& c : text)
    if (c < 32 || c > 126) c = '?';
  while (!text.empty() && text.back() == ' ') text.pop_back();
  return true;
}

bool parse_position(const Data& data, Position& position) {
  if (data.portnum != kPositionApp && data.portnum != 3) return false;
  const auto& bytes = data.payload;
  size_t pos = 0;
  bool have_lat = false, have_lon = false;
  while (pos < bytes.size()) {
    uint64_t tag = 0;
    if (!read_varint(bytes, pos, tag)) break;
    const uint32_t field = uint32_t(tag >> 3);
    const uint32_t wire = uint32_t(tag & 0x07);
    if (wire == 5) {  // fixed32, signed for latitude_i / longitude_i
      if (pos + 4 > bytes.size()) break;
      const int32_t raw = int32_t(read_le32(bytes.data() + pos));
      pos += 4;
      if (field == 1) {
        position.latitude = raw / 1e7;
        have_lat = true;
      } else if (field == 2) {
        position.longitude = raw / 1e7;
        have_lon = true;
      }
    } else if (wire == 0) {
      uint64_t value = 0;
      if (!read_varint(bytes, pos, value)) break;
      if (field == 3) {
        position.altitude = int32_t(value);
        position.has_altitude = true;
      } else if (field == 4) {
        position.time = uint32_t(value);
        position.has_time = true;
      }
    } else if (wire == 2) {
      uint64_t length = 0;
      if (!read_varint(bytes, pos, length)) break;
      if (length > bytes.size() - pos) break;
      pos += size_t(length);
    } else if (wire == 1) {
      pos += 8;
    } else {
      break;
    }
  }
  if (!have_lat || !have_lon) return false;
  if (position.latitude < -90.0 || position.latitude > 90.0) return false;
  if (position.longitude < -180.0 || position.longitude > 180.0) return false;
  return true;
}

const char* port_name(uint32_t portnum) {
  switch (portnum) {
    case kTextMessageApp: return "TEXT";
    case kPositionApp: return "POSITION";
    case kNodeInfoApp: return "NODEINFO";
    case kRoutingApp: return "ROUTING";
    case kTelemetryApp: return "TELEMETRY";
    default: return "OTHER";
  }
}

}  // namespace orc::zero::mesh::meshtastic

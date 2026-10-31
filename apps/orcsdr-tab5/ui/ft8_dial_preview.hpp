#pragma once

#include "ft8_model.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

// Receiver-side producer for OrcDial's optional read-only FT8 decode preview.
// This is a portable, allocation-free codec. It DOES NOT transmit anything.
// The Tab5 link owner must only send these frames to a paired, authenticated
// Dial that explicitly negotiated ft8-decode-preview support. The required
// sender is the same sender identity used by the existing RADIO_STATE frames.
namespace orcsdr::ft8::dial_preview {
constexpr size_t frame_size = 64;
constexpr uint32_t magic = 0x4c44524f;
constexpr uint8_t protocol = 4, role_receiver = 2;
constexpr uint8_t brief_type = 17, text_type = 18; // appended, never renumber v4 commands
constexpr uint8_t time_valid = 1u << 0;
constexpr uint8_t snr_valid = 1u << 1;
constexpr uint8_t grid_associated = 1u << 2;
constexpr uint8_t assisted = 1u << 3;
constexpr uint8_t message_truncated = 1u << 4;
inline void put32(uint8_t* out, uint32_t v) {
  out[0] = uint8_t(v); out[1] = uint8_t(v >> 8);
  out[2] = uint8_t(v >> 16); out[3] = uint8_t(v >> 24);
}
inline uint32_t crc32(const uint8_t* src, size_t n) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < n; ++i) {
    crc ^= src[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1u));
  }
  return ~crc;
}
// Reject malformed source strings rather than serializing arbitrary device
// memory or control bytes. All output slots are padded with zeroes.
inline bool copy_text(const char* src, size_t src_capacity, uint8_t* dst, size_t dst_capacity,
                      bool* truncated = nullptr) {
  std::memset(dst, 0, dst_capacity);
  if (!src || !src_capacity || !dst_capacity) return false;
  size_t n = 0;
  for (; n < src_capacity && src[n] != 0; ++n)
    if (uint8_t(src[n]) < 32 || uint8_t(src[n]) > 126) return false;
  if (n == src_capacity) return false; // not terminated by source buffer
  const size_t copy = n < dst_capacity ? n : dst_capacity - 1;
  std::memcpy(dst, src, copy);
  if (truncated) *truncated = n > copy;
  return true;
}
inline void head(uint8_t out[frame_size], uint8_t type,
                 uint32_t sender, uint32_t record_id) {
  std::memset(out, 0, frame_size);
  put32(out, magic); out[4] = protocol; out[5] = type; out[6] = role_receiver;
  put32(out + 8, sender); put32(out + 12, record_id);
}
inline void finish(uint8_t out[frame_size]) { put32(out + 60, crc32(out, 60)); }

// A valid decoded message with *capture-time* dial frequency; no current
// tuning, guessed grid or callbook location is substituted for missing data.
inline bool encode(const Decode& decoded, uint32_t sender, uint32_t record_id,
                   uint8_t brief[frame_size], uint8_t text[frame_size]) {
  if (!sender || !record_id || decoded.dial_hz < 24000 || decoded.dial_hz > 1766000000u ||
      uint8_t(decoded.mode) >= uint8_t(DigitalMode::count)) return false;
  uint8_t b[frame_size]{}, t[frame_size]{};
  head(b, brief_type, sender, record_id);
  head(t, text_type, sender, record_id);
  put32(b + 20, decoded.utc_epoch);
  put32(b + 24, decoded.dial_hz);
  if (!copy_text(decoded.callsign, sizeof(decoded.callsign), b + 28, 16)) return false;
  uint8_t source_grid[9]{};
  bool was_truncated = false;
  if (!copy_text(decoded.grid, sizeof(decoded.grid), source_grid, sizeof(source_grid)) ||
      !copy_text(decoded.message, sizeof(decoded.message), t + 20, 40, &was_truncated)) return false;
  uint8_t flags = decoded.utc_epoch ? time_valid : 0;
  // Only a CQ-associated grid has an unambiguous transmitter mapping here.
  // Other message formats need a stricter record-level association contract.
  char cq_call[16]{}, cq_grid[9]{};
  if (decoded.kind == DecodeKind::cq && maidenhead_valid(decoded.grid) &&
      parse_cq_fields(decoded.message, cq_call, sizeof(cq_call), cq_grid, sizeof(cq_grid)) &&
      !std::strcmp(cq_call, decoded.callsign) && !std::strcmp(cq_grid, decoded.grid)) {
    if (!copy_text(decoded.grid, sizeof(decoded.grid), b + 44, 9)) return false;
    flags |= grid_associated;
  }
  if (!(decoded.flags & decode_flag_snr_unavailable) &&
      decoded.snr_db >= -128 && decoded.snr_db <= 127) {
    b[53] = uint8_t(int8_t(decoded.snr_db));
    flags |= snr_valid;
  }
  if (decoded.flags & decode_flag_assisted) flags |= assisted;
  if (was_truncated) flags |= message_truncated;
  b[54] = flags; b[55] = uint8_t(decoded.mode);
  finish(b); finish(t);
  std::memcpy(brief, b, frame_size); std::memcpy(text, t, frame_size);
  return true;
}
} // namespace orcsdr::ft8::dial_preview

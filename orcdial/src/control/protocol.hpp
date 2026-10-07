#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace orc {

// Control payload codec: accepted only inside a version-4 encrypted session.
constexpr uint8_t version = 4;
constexpr size_t packet_size = 64;
constexpr uint32_t magic = 0x4c44524f; // ORDL in little-endian bytes

enum class Type : uint8_t {
  hello = 1, pair_request, pair_ack, heartbeat, tune_relative,
  tune_absolute, set_step, set_mode, set_gain, set_volume,
  set_squelch, request_state, radio_state, error, set_dashboard, semantic_action
};
enum class Role : uint8_t { dial = 1, receiver = 2 };

struct Packet {
  Type type = Type::hello;
  Role role = Role::dial;
  uint32_t sender = 0;
  uint32_t sequence = 0;
  uint32_t ack = 0;
  int32_t value = 0;
  uint32_t frequency_hz = 0;
  uint32_t step_hz = 5000;
  int16_t gain_tenth_db = 0;
  int16_t squelch = 0;
  int16_t signal_dbm = 0;
  uint8_t mode = 0;
  uint8_t volume = 0;
  uint8_t flags = 0; // bit 0: signal measurement is valid
  uint8_t dashboard = 0;
  uint8_t action = 0; // semantic ActionKind for SEMANTIC_ACTION
  uint8_t view = 0;
  uint8_t band = 0; // Tab5 band profile id + 1 for the frequency on screen; 0 = unknown (byte 7, was reserved)
  uint32_t revision = 0;
  int32_t selected = 0;
  uint32_t item_count = 0;
  uint32_t capabilities = 0;
};

inline void put16(uint8_t* p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
inline void put32(uint8_t* p, uint32_t n) {
  for (int i = 0; i < 4; ++i) p[i] = n >> (8 * i);
}
inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1u));
  }
  return ~crc;
}
inline bool valid_type(uint8_t n) {
  return n >= uint8_t(Type::hello) && n <= uint8_t(Type::semantic_action);
}
inline bool newer_sequence(uint32_t candidate, uint32_t previous) {
  return previous == 0 || int32_t(candidate - previous) > 0;
}
inline void encode(const Packet& p, uint8_t out[packet_size]) {
  std::memset(out, 0, packet_size);
  put32(out, magic);
  out[4] = version; out[5] = uint8_t(p.type); out[6] = uint8_t(p.role); out[7] = p.band;
  put32(out + 8, p.sender); put32(out + 12, p.sequence); put32(out + 16, p.ack);
  put32(out + 20, uint32_t(p.value)); put32(out + 24, p.frequency_hz);
  put32(out + 28, p.step_hz);
  put16(out + 32, uint16_t(p.gain_tenth_db));
  put16(out + 34, uint16_t(p.squelch));
  put16(out + 36, uint16_t(p.signal_dbm));
  out[38] = p.mode; out[39] = p.volume; out[40] = p.flags;
  out[41] = p.dashboard; out[42] = p.action; out[43] = p.view;
  put32(out + 44, p.revision); put32(out + 48, uint32_t(p.selected));
  put32(out + 52, p.item_count); put32(out + 56, p.capabilities);
  put32(out + 60, crc32(out, 60));
}
inline bool decode(const uint8_t* in, size_t size, Packet& p) {
  if (!in || size != packet_size || get32(in) != magic || in[4] != version ||
      !valid_type(in[5]) || (in[6] != 1 && in[6] != 2) ||
      get32(in + 60) != crc32(in, 60)) return false;
  p.type = Type(in[5]); p.role = Role(in[6]); p.band = in[7];
  p.sender = get32(in + 8); p.sequence = get32(in + 12); p.ack = get32(in + 16);
  p.value = int32_t(get32(in + 20)); p.frequency_hz = get32(in + 24);
  p.step_hz = get32(in + 28);
  p.gain_tenth_db = int16_t(get16(in + 32)); p.squelch = int16_t(get16(in + 34));
  p.signal_dbm = int16_t(get16(in + 36));
  p.mode = in[38]; p.volume = in[39]; p.flags = in[40]; p.dashboard = in[41];
  p.action = in[42]; p.view = in[43]; p.revision = get32(in + 44);
  p.selected = int32_t(get32(in + 48)); p.item_count = get32(in + 52);
  p.capabilities = get32(in + 56);
  return true;
}

} // namespace orc

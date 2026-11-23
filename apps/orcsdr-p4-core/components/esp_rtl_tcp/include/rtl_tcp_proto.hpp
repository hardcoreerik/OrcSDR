#pragma once
// rtl_tcp wire protocol, as observed from the original program (docs/hardware/ESP_RTL_TCP_DESIGN.md section 2). Pure C++, no platform headers.
//   server -> client, once, right after accept: "RTL0" + tuner type (u32 big-endian) + tuner gain count (u32 big-endian)  = 12 bytes
//   server -> client afterwards: raw unsigned 8-bit interleaved I/Q, nothing else
//   client -> server: 5-byte commands, code (u8) + parameter (u32 big-endian), no replies
#include <cstddef>
#include <cstdint>

namespace esp_rtl_tcp::proto {

constexpr size_t kHeaderBytes = 12;
constexpr size_t kCommandBytes = 5;

// Values of the original's tuner enum that the header carries. The driver's own enum is different; hosts map to these.
enum TunerType : uint32_t { kTunerUnknown = 0, kTunerE4000 = 1, kTunerFc0012 = 2, kTunerFc0013 = 3, kTunerFc2580 = 4, kTunerR820T = 5, kTunerR828D = 6 };

inline uint32_t read_be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]); }
inline void write_be32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24);
  p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);
  p[3] = uint8_t(v);
}

inline void encode_header(uint8_t out[kHeaderBytes], uint32_t tuner_type, uint32_t gain_count) {
  out[0] = 'R';
  out[1] = 'T';
  out[2] = 'L';
  out[3] = '0';
  write_be32(out + 4, tuner_type);
  write_be32(out + 8, gain_count);
}

// What a command code means. The numbers are the original's (0x01 to 0x0E); anything else is `unknown` and is counted, never applied.
enum class Op : uint8_t {
  unknown,
  set_center,       // 0x01 centre frequency, Hz
  set_rate,         // 0x02 sample rate, S/s
  gain_mode,        // 0x03 0 = automatic, nonzero = manual
  gain,             // 0x04 tuner gain, signed tenth dB
  ppm,              // 0x05 frequency correction, signed ppm
  if_gain,          // 0x06 IF stage gain: no equivalent here
  test_mode,        // 0x07 test pattern: unsupported
  rtl_agc,          // 0x08 RTL2832 AGC
  direct_sampling,  // 0x09 unsupported
  offset_tuning,    // 0x0A unsupported
  xtal,             // 0x0B / 0x0C crystal frequencies: unsupported
  gain_index,       // 0x0D gain by ladder index
  bias_tee,         // 0x0E
};

inline Op classify(uint8_t code) {
  switch (code) {
    case 0x01: return Op::set_center;
    case 0x02: return Op::set_rate;
    case 0x03: return Op::gain_mode;
    case 0x04: return Op::gain;
    case 0x05: return Op::ppm;
    case 0x06: return Op::if_gain;
    case 0x07: return Op::test_mode;
    case 0x08: return Op::rtl_agc;
    case 0x09: return Op::direct_sampling;
    case 0x0A: return Op::offset_tuning;
    case 0x0B:
    case 0x0C: return Op::xtal;
    case 0x0D: return Op::gain_index;
    case 0x0E: return Op::bias_tee;
    default: return Op::unknown;
  }
}

// True for the ops this server knows but cannot perform.
inline bool is_unsupported(Op op) { return op == Op::if_gain || op == Op::test_mode || op == Op::direct_sampling || op == Op::offset_tuning || op == Op::xtal; }

struct Command {
  uint8_t code;
  uint32_t param;
};

// Reassembles the client's byte stream into whole 5-byte commands (TCP delivers any split).
class CommandStream {
 public:
  // Feeds bytes; calls `fn(const Command&)` once per complete command, in order.
  template <typename Fn>
  void feed(const uint8_t* data, size_t len, Fn&& fn) {
    for (size_t i = 0; i < len; ++i) {
      buf_[fill_++] = data[i];
      if (fill_ == kCommandBytes) {
        fn(Command{buf_[0], read_be32(buf_ + 1)});
        fill_ = 0;
      }
    }
  }
  void reset() { fill_ = 0; }
  size_t pending() const { return fill_; }

 private:
  uint8_t buf_[kCommandBytes] = {};
  size_t fill_ = 0;
};

}  // namespace esp_rtl_tcp::proto

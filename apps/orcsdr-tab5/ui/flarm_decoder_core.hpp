#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::flarm_rx {

constexpr uint32_t kCenterHz = 868300000;
constexpr uint32_t kSampleRate = 960000;
constexpr size_t kPacketBytes = 26; // 24 payload + big-endian CCITT CRC

struct Context {
  // UTC at the first IQ sample, not uptime. Zero means unavailable.
  uint64_t unix_ms = 0;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  bool location_valid = false;
};

struct Frame {
  uint32_t address = 0;
  uint8_t address_type = 0;
  uint8_t generation = 0; // AIR V6 or AIR V7
  uint8_t aircraft_type = 0;
  uint8_t channel = 0;
  double latitude = 0;
  double longitude = 0;
  float altitude_m = 0; // WGS84 ellipsoid, NOT pressure altitude or flight level
  float speed_mps = 0;
  float course_deg = 0;
  float climb_mps = 0;
  float signal_dbfs = -120;
  bool stealth = false;
  bool no_track = false;
};

enum class Result : uint8_t { ok, crc_error, need_time, need_location, unsupported, invalid };
struct Stats {
  uint32_t syncs = 0;
  uint32_t crc_ok = 0;
  uint32_t crc_errors = 0;
  uint32_t invalid = 0;
  uint32_t unsupported = 0;
  uint32_t v6 = 0;
  uint32_t v7 = 0;
};
using Callback = void (*)(const Frame&, void*);

// Packet seam for recorded on-air bytes and independent reference vectors.
// Expects Manchester-decoded bytes, with the RF payload inversion removed.
uint16_t packet_crc(const uint8_t* payload);
Result decode_packet(const uint8_t* bytes, size_t length, const Context&, Frame*);
const char* aircraft_type_name(uint8_t type);

// Two continuously sampled EU channels; fixed storage, no platform dependencies.
class Decoder {
 public:
  Decoder();
  void reset();
  // Preserve statistics but discard symbol/filter state after a dropped IQ block.
  void discontinuity();
  void process_cu8(const uint8_t*, size_t, const Context&, Callback, void*);
  const Stats& stats() const { return stats_; }

 private:
  static constexpr size_t kTaps = 33;
  static constexpr size_t kLanes = 8;
  struct Lane {
    uint32_t phase = 0;
    float sum = 0;
    uint64_t sync = 0;
    uint16_t chips = 0;
    uint8_t first = 0;
    bool collecting = false;
    bool inverted = false;
    uint8_t packet[kPacketBytes]{};
  };
  struct Channel {
    float i[kTaps]{}, q[kTaps]{};
    float prev_i = 0, prev_q = 0;
    float power = 0;
    uint64_t last_frame_sample = 0;
    bool has_frame = false;
    Lane lanes[kLanes]{};
  };
  void chip(Channel&, Lane&, bool, uint8_t, const Context&, Callback, void*);
  Channel channels_[2]{};
  float taps_[kTaps]{}, osc_i_[48]{}, osc_q_[48]{};
  size_t ring_ = 0, oscillator_ = 0;
  uint64_t samples_ = 0;
  bool pending_i_ = false;
  uint8_t i_byte_ = 0;
  Stats stats_{};
};

} // namespace orcsdr::flarm_rx

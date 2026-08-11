#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::adsb_rx {

struct Frame {
  uint8_t bytes[14]{};
  uint8_t bit_length = 0;
  uint32_t icao = 0;
  uint8_t type_code = 0;
  char callsign[9]{};
  bool has_callsign = false;
  int altitude_ft = 0;
  bool has_altitude = false;
  int speed_kts = 0;
  bool has_speed = false;
  int heading_deg = 0;
  bool has_heading = false;
  int vertical_rate_fpm = 0;
  bool has_vertical_rate = false;
  uint32_t cpr_latitude = 0;
  uint32_t cpr_longitude = 0;
  bool cpr_odd = false;
  bool has_cpr = false;
  uint16_t signal = 0;
};

bool decode_global_cpr(const Frame& first, const Frame& second, bool use_odd,
                       double* latitude, double* longitude);

struct Stats {
  uint32_t preambles = 0;
  uint32_t frames = 0;
  uint32_t df17 = 0;
  uint32_t crc_ok = 0;
  uint16_t magnitude_min = 0xffff;
  uint16_t magnitude_max = 0;
};

using FrameCallback = void (*)(const Frame&, void*);

class Decoder {
 public:
  void reset();
  void process_cu8(const uint8_t* data, size_t bytes, FrameCallback callback, void* context);
  const Stats& stats() const { return stats_; }
 static bool self_check();

 private:
  static constexpr size_t kMagnitudeCapacity = 16640;
  uint16_t magnitudes_[kMagnitudeCapacity]{};
  size_t overlap_ = 0;
  Stats stats_{};
};

}  // namespace orcsdr::adsb_rx

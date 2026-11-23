#pragma once
#include "dead_air_model.hpp"
#include <cstdint>

// Environment events: a temperature, humidity or pressure reading that leaves its own recent baseline and stays away.
// Pure logic (no I2C, no clock): the caller feeds each accepted sensor sample once and appends the returned finding
// through EventModel::append_measured. An event is a measured departure from the room's own baseline, never a claim
// about its cause; a breath on the sensor and a draught both produce one.
namespace orcsdr::dead_air {

enum class EnvChannel : uint8_t { temperature, humidity, pressure, count };

struct EnvFinding {
  EnvChannel channel = EnvChannel::temperature;
  float value = 0.0f;      // the reading that crossed (C, %RH or hPa)
  float baseline = 0.0f;   // the room's baseline at that moment
  float threshold = 0.0f;  // the departure that was required (always positive; the event may be a rise or a fall)
};

float env_threshold(EnvChannel channel, Sensitivity sensitivity);  // C, %RH, hPa
uint32_t env_hold_ms(EnvChannel channel);                          // how long a departure must last
Kind env_kind(EnvChannel channel);
Event env_event(const EnvFinding& finding, uint32_t monotonic_ms, uint32_t unix_time, Sensitivity sensitivity);

class EnvEventDetector {
 public:
  static constexpr uint32_t kWarmupSamples = 15;     // valid samples before a baseline exists (about 15 s at 1 Hz)
  static constexpr uint32_t kGapMs = 5000;           // longer between samples = the sensor was interrupted: start over
  static constexpr uint32_t kRebaseMs = 300000;      // a departure that lasts this long is the new normal: re-learn
  static constexpr float kAdapt = 0.05f;             // baseline follows slow drift (about a 20 s time constant at 1 Hz)

  void reset();
  void reset(EnvChannel channel);
  // `sample_ms` is when the sensor measured the value. Returns true once per departure.
  bool observe(EnvChannel channel, float value, uint32_t sample_ms, Sensitivity sensitivity, EnvFinding* out);

  bool baseline_ready(EnvChannel channel) const { return chan_[index(channel)].count >= kWarmupSamples; }
  float baseline(EnvChannel channel) const { return chan_[index(channel)].baseline; }

 private:
  struct Channel {
    float baseline = 0.0f;
    uint32_t count = 0;
    bool have_last = false;
    uint32_t last_ms = 0;
    bool pending = false, latched = false;
    int8_t direction = 0;
    uint32_t since_ms = 0, latched_ms = 0;
  };
  static size_t index(EnvChannel c) { return static_cast<size_t>(c); }
  Channel chan_[static_cast<size_t>(EnvChannel::count)];
};

}  // namespace orcsdr::dead_air

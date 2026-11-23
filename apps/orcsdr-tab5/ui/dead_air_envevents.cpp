#include "dead_air_envevents.hpp"
#include <cmath>

namespace orcsdr::dead_air {

float env_threshold(EnvChannel channel, Sensitivity sensitivity) {
  const int s = sensitivity == Sensitivity::quiet ? 0 : sensitivity == Sensitivity::sensitive ? 2 : 1;
  static constexpr float kTemperature[3] = {1.5f, 1.0f, 0.5f};  // C
  static constexpr float kHumidity[3] = {6.0f, 4.0f, 2.0f};     // %RH
  static constexpr float kPressure[3] = {0.8f, 0.5f, 0.25f};    // hPa
  switch (channel) {
    case EnvChannel::humidity: return kHumidity[s];
    case EnvChannel::pressure: return kPressure[s];
    default: return kTemperature[s];
  }
}

uint32_t env_hold_ms(EnvChannel channel) { return channel == EnvChannel::pressure ? 3000u : 5000u; }

Kind env_kind(EnvChannel channel) {
  switch (channel) {
    case EnvChannel::humidity: return Kind::humidity_change;
    case EnvChannel::pressure: return Kind::pressure_change;
    default: return Kind::temperature_change;
  }
}

Event env_event(const EnvFinding& finding, uint32_t monotonic_ms, uint32_t unix_time, Sensitivity sensitivity) {
  Event e{};
  e.monotonic_ms = monotonic_ms;
  e.unix_time = unix_time;
  e.source = Source::environment;
  e.kind = env_kind(finding.channel);
  e.value = finding.value;
  e.reference = finding.baseline;
  e.threshold = finding.threshold;
  e.sensitivity = sensitivity;
  return e;
}

void EnvEventDetector::reset() {
  for (Channel& c : chan_) c = Channel{};
}

void EnvEventDetector::reset(EnvChannel channel) { chan_[index(channel)] = Channel{}; }

bool EnvEventDetector::observe(EnvChannel channel, float value, uint32_t sample_ms, Sensitivity sensitivity,
                               EnvFinding* out) {
  Channel& c = chan_[index(channel)];
  if (!std::isfinite(value)) {
    c = Channel{};
    return false;
  }
  // A sample that is not newer, or a long silence, cannot continue an observation window.
  if (c.have_last) {
    const uint32_t elapsed = sample_ms - c.last_ms;  // unsigned: handles 32-bit uptime rollover
    if (static_cast<int32_t>(elapsed) <= 0) return false;
    if (elapsed > kGapMs) c = Channel{};
  }
  c.last_ms = sample_ms;
  c.have_last = true;
  if (c.count < kWarmupSamples) {
    c.baseline = (c.baseline * static_cast<float>(c.count) + value) / static_cast<float>(c.count + 1);
    ++c.count;
    return false;
  }
  const float threshold = env_threshold(channel, sensitivity);
  const float delta = value - c.baseline;
  const float magnitude = std::fabs(delta);
  if (magnitude <= threshold * 0.5f) {
    c.pending = c.latched = false;
    c.baseline += kAdapt * delta;  // adapt only when there is no candidate departure
    return false;
  }
  if (c.latched) {
    // A departure that never ends is a new normal (a door shut, the heating came on): learn it instead of staying armed forever.
    if (static_cast<uint32_t>(sample_ms - c.latched_ms) >= kRebaseMs) {
      c = Channel{};
      c.last_ms = sample_ms;
      c.have_last = true;
      c.baseline = value;
      c.count = 1;
    }
    return false;
  }
  if (magnitude < threshold) {
    c.pending = false;
    return false;
  }
  const int8_t direction = delta > 0 ? 1 : -1;
  if (!c.pending || c.direction != direction) {
    c.pending = true;
    c.direction = direction;
    c.since_ms = sample_ms;
    return false;
  }
  if (static_cast<uint32_t>(sample_ms - c.since_ms) < env_hold_ms(channel)) return false;
  if (out) {
    out->channel = channel;
    out->value = value;
    out->baseline = c.baseline;
    out->threshold = threshold;
  }
  c.pending = false;
  c.latched = true;
  c.latched_ms = sample_ms;
  return true;
}

}  // namespace orcsdr::dead_air

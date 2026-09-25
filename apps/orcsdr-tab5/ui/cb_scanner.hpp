#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::cb {

constexpr size_t kChannelCount = 40;

// FCC Part 95 Subpart D 40-channel plan. Channel 23 sits out of numeric order.
inline constexpr uint32_t kChannelsHz[kChannelCount] = {
    26965000, 26975000, 26985000, 27005000, 27015000, 27025000, 27035000,
    27055000, 27065000, 27075000, 27085000, 27105000, 27115000, 27125000,
    27135000, 27155000, 27165000, 27175000, 27185000, 27205000, 27215000,
    27225000, 27255000, 27235000, 27245000, 27265000, 27275000, 27285000,
    27295000, 27305000, 27315000, 27325000, 27335000, 27345000, 27355000,
    27365000, 27375000, 27385000, 27395000, 27405000};

constexpr size_t kEmergencyChannel = 8;   // CH 9
constexpr size_t kHighwayChannel = 18;    // CH 19
constexpr size_t kFirstSsbChannel = 35;   // CH 36; 36-40 carry SSB by convention
constexpr float kNoLevel = -200.0f;
constexpr uint32_t kChannelHalfWidthHz = 4000;

// Common-use label for display only; it never implies station identity.
const char* channel_note(size_t channel);
size_t nearest_channel(uint32_t frequency_hz);

// Peak spectrum level inside +/-4 kHz of each channel. `bins` is an
// fft-shifted power spectrum in dB centered on `center_hz`. Channels whose
// passband falls outside the usable (anti-alias safe) span get kNoLevel.
void channel_levels(const float* bins, size_t bin_count, uint32_t sample_rate_sps,
                    uint32_t center_hz, float out[kChannelCount]);
// Median of the valid channel levels: a robust per-frame band noise floor.
float noise_floor(const float levels[kChannelCount]);

struct Hit {
  uint8_t channel = 0;
  uint32_t start_ms = 0;
  uint32_t duration_ms = 0;
  float peak_snr_db = 0.0f;
};

struct ChannelStats {
  float snr_db = 0.0f;
  float peak_snr_db = 0.0f;
  uint32_t active_ms = 0;
  uint32_t last_active_ms = 0;
  uint16_t hits = 0;
  bool active = false;
  bool seen = false;
};

// Watches all 40 channels at once from the wideband spectrum and keeps a
// bounded activity log. Independent of which channel is being demodulated.
class Monitor {
 public:
  static constexpr size_t kLogCapacity = 48;
  static constexpr float kHysteresisDb = 3.0f;
  static constexpr uint32_t kReleaseMs = 700;
  static constexpr uint32_t kMinHitMs = 250;

  void reset();
  void clear_log();
  void observe(uint32_t now_ms, const float levels[kChannelCount], float floor_db,
               float threshold_db);
  bool active(size_t channel) const;
  const ChannelStats& stats(size_t channel) const { return stats_[channel]; }
  float floor_db() const { return floor_db_; }
  size_t log_count() const { return log_count_; }
  // 0 is the newest entry.
  const Hit* log(size_t index) const;
  size_t active_count() const;

 private:
  struct Episode {
    uint32_t start_ms = 0;
    uint32_t last_above_ms = 0;
    float peak_snr_db = 0.0f;
    bool open = false;
  };
  void close_episode(size_t channel);
  void push(const Hit& hit);

  ChannelStats stats_[kChannelCount]{};
  Episode episodes_[kChannelCount]{};
  Hit log_[kLogCapacity]{};
  size_t log_head_ = 0;
  size_t log_count_ = 0;
  float floor_db_ = kNoLevel;
  uint32_t last_ms_ = 0;
  bool have_last_ = false;
};

enum class State : uint8_t { off, watching, settling, receiving, hang, held };

struct Settings {
  float threshold_db = 10.0f;
  uint16_t hang_ms = 2000;
  uint16_t settle_ms = 350;
  uint16_t max_hold_s = 0;  // 0 keeps a busy channel indefinitely
  uint8_t priority_channel = kEmergencyChannel;
  bool priority_enabled = true;
  bool auto_sideband = true;  // LSB on the SSB channels 36-40, AM below
};

constexpr float kThresholdMinDb = 4.0f;
constexpr float kThresholdMaxDb = 30.0f;
constexpr uint16_t kHangMinMs = 0;
constexpr uint16_t kHangMaxMs = 8000;
constexpr uint16_t kMaxHoldChoicesS[] = {0, 15, 30, 60, 120};

// Band-wide scan: pick an active, eligible channel; hold while it talks, wait
// the hang time for a reply, then return to watching. Priority preempts.
class Scanner {
 public:
  void start(uint32_t now_ms, size_t tuned_channel);
  void stop();
  void hold(size_t tuned_channel);
  void release(uint32_t now_ms);
  void skip(uint32_t now_ms);
  // Returns the channel to retune to, or -1 when no retune is needed.
  int update(uint32_t now_ms, const Monitor& monitor);
  void note_manual_tune(size_t channel);

  bool running() const { return state_ != State::off; }
  State state() const { return state_; }
  size_t channel() const { return channel_; }
  uint32_t hang_remaining_ms(uint32_t now_ms) const;
  uint32_t stops() const { return stops_; }

  Settings& settings() { return settings_; }
  const Settings& settings() const { return settings_; }
  void set_lockout(size_t channel, bool locked);
  bool locked_out(size_t channel) const;
  bool skipped(size_t channel) const;
  void clear_lockouts();
  uint64_t lockout_mask() const { return lockouts_; }
  void set_lockout_mask(uint64_t mask);
  size_t eligible_count() const;

  static bool self_check();

 private:
  bool eligible(size_t channel) const;
  int choose(const Monitor& monitor) const;
  int go(size_t channel, uint32_t now_ms);

  Settings settings_{};
  State state_ = State::off;
  size_t channel_ = kHighwayChannel;
  uint32_t settle_until_ms_ = 0;
  uint32_t hang_until_ms_ = 0;
  uint32_t rx_started_ms_ = 0;
  uint64_t lockouts_ = 0;
  uint64_t skips_ = 0;
  uint32_t stops_ = 0;
};

const char* state_name(State state);

}  // namespace orcsdr::cb

#pragma once
// Digital Voice wideband survey: finds which channels of the picked bands carry a signal BEFORE any decoder looks at them.
//
// The decoder chain needs 240 kS/s and sees about 16 channels per tune. This module asks the receiver for the highest sample
// rate that stays free of lost samples (2.56 MS/s down to 1.02 MS/s), so one tune shows well over a hundred 12.5 kHz channels.
// It takes a short capture (8 blocks), runs an averaged FFT and compares each channel's power with the noise floor of the same
// window (the 25th percentile of the window's channels: never a fixed level, so it adapts to the dongle, antenna and place).
// Channels clearly above the floor go into the hot list; only those are handed to the slow decode scan.
//
// Pure logic: no radio, no allocation, no UI. The receiver owner performs the tune (service() asks, tune_result() answers) and the
// DSP task feeds blocks through offer(), which only copies a few KB. All analysis runs in the UI task. Receive only.
// Levels are relative (uncalibrated) dB; the module never claims a signal is DMR, only that a channel is occupied.
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "dv_scanner.hpp"

namespace orcsdr::dv_survey {

constexpr size_t kFft = 1024;
constexpr size_t kFrames = 8;                 // independent 1024-sample frames averaged per window
constexpr size_t kMaxHot = 128;               // hot channels kept per pass (strongest first)
// Skirt rule: a strong carrier raises the channels beside it (splatter, clipping products: 14 to 18 dB on the 2026-11-25 453 MHz capture). At the end of a pass, a hot
// channel with another hot channel within kSkirtHz whose margin over its window floor is at least kSkirtDb larger is a skirt, not a signal, and is not sent to the
// decoders. It is applied to the whole pass, not per window: a strong carrier inside one window's tuner-centre guard zone is invisible there but seen by the overlapping window.
constexpr float kSkirtDb = 15.0f;
constexpr uint32_t kSkirtHz = 25000;
constexpr size_t kMaxWindowChannels = 400;    // channels evaluated per window
constexpr size_t kRates = 5;
constexpr uint32_t kRateLadder[kRates] = {2560000, 2400000, 2048000, 1536000, 1024000};   // highest first; driver-supported
constexpr uint32_t kChannelHz = 12500;
constexpr uint32_t kDcGuardHz = 30000;        // the tuner's centre spike: channels this close to the centre come from the neighbouring window

// Working memory (about 41 KB): the caller allocates it in PSRAM (the Tab5 internal boot pool is nearly full).
struct Work {
  float re[kFft], im[kFft], pw[kFft], win[kFft], twr[kFft / 2], twi[kFft / 2];
  uint8_t cap[kFrames * kFft * 2];
  float lv[kMaxWindowChannels];
  uint32_t lf[kMaxWindowChannels];
};
void init(Work& w) noexcept;                  // Hann window and FFT twiddles
// Averaged power spectrum of the kFrames frames in w.cap, FFT-shifted (index 0 = -rate/2) into w.pw (linear power).
void analyze(Work& w) noexcept;
// Mean power (dB, relative) of the bins inside the 12.5 kHz channel `offset_hz` from the tune centre. -200 when outside the span.
float channel_db(const Work& w, uint32_t rate, int32_t offset_hz) noexcept;

struct Hot { uint32_t hz = 0; float margin_db = 0; };

enum class State : uint8_t { idle, tuning, settling, capturing, done, failed };

struct Config {
  uint32_t settle_ms = 120;       // after an accepted tune: samples from the old centre may still be in flight
  uint32_t capture_ms_max = 700;  // a window that has not filled by then is retried
  float margin_db = 7.0f;         // above the window's noise floor to count as occupied
  uint8_t max_retries = 2;        // per window, before stepping the rate down
};

class Survey {
 public:
  explicit Survey(const Config& c = {}) noexcept : cfg_(c) {}
  void attach(Work* w) noexcept { work_ = w; }
  // Plan: the segments of the picked bands, surveyed in order. False without memory, segments or a first rate.
  bool start(const dv::Segment* segs, size_t n, uint32_t now_ms, uint8_t first_rate_index = 0) noexcept;
  void stop() noexcept;
  // UI task. True = tune now (to *tune_hz at *tune_rate), then call tune_result(). `lost_total` = monotonic count of IQ samples
  // lost upstream (any movement during a capture discards that window).
  bool service(uint32_t now_ms, uint64_t lost_total, uint32_t* tune_hz, uint32_t* tune_rate) noexcept;
  void tune_result(bool accepted, uint32_t now_ms) noexcept;
  // DSP task: one CU8 block. Copies at most one frame; never blocks.
  void offer(const uint8_t* cu8, size_t bytes, uint32_t sample_rate) noexcept;
  bool capturing() const noexcept { return state_.load(std::memory_order_acquire) == State::capturing; }

  State state() const noexcept { return state_.load(std::memory_order_acquire); }
  bool finished() const noexcept { return state() == State::done; }
  bool failed() const noexcept { return state() == State::failed; }
  uint32_t rate() const noexcept { return kRateLadder[rate_i_]; }
  uint8_t rate_index() const noexcept { return rate_i_; }
  uint32_t windows_done() const noexcept { return windows_done_; }
  uint32_t windows_total() const noexcept;     // estimate at the current rate
  uint32_t lossy_windows() const noexcept { return lossy_; }
  uint32_t rate_steps() const noexcept { return rate_steps_; }
  uint32_t channels_checked() const noexcept { return checked_; }
  uint32_t skirts_dropped() const noexcept { return skirts_; }
  uint32_t current_center_hz() const noexcept { return center_hz_; }
  size_t hot_count() const noexcept { return hot_n_; }
  const Hot& hot(size_t i) const noexcept { return hot_[i]; }
  float last_floor_db() const noexcept { return last_floor_; }

 private:
  void plan_window() noexcept;      // next window centre, or done
  void evaluate() noexcept;         // analyze + hot channels of the captured window
  void add_hot(uint32_t hz, float margin) noexcept;
  void trim_skirts() noexcept;
  void window_failed(uint32_t now_ms, bool lossy) noexcept;

  Config cfg_;
  Work* work_ = nullptr;
  dv::Segment segs_[dv::kMaxSegments]{};
  size_t nseg_ = 0, seg_ = 0;
  int64_t centre_ = 0;              // next window centre in the current segment (can start below the segment)
  std::atomic<State> state_{State::idle};
  std::atomic<uint8_t> frames_{0};
  uint8_t rate_i_ = 0, retries_ = 0, rate_loss_run_ = 0;
  uint32_t center_hz_ = 0, state_since_ms_ = 0, windows_done_ = 0, lossy_ = 0, rate_steps_ = 0, checked_ = 0, skirts_ = 0;
  uint64_t lost_at_arm_ = 0;
  float last_floor_ = 0;
  Hot hot_[kMaxHot]{};
  size_t hot_n_ = 0;
  bool tune_pending_ = false, seg_started_ = false;
};

// The one survey the DSP tap feeds (installed by the app while it owns a survey, null otherwise).
void install(Survey* s) noexcept;
bool capturing() noexcept;
bool active() noexcept;       // tuning, settling or capturing: the DSP task skips its normal work for wideband blocks
void offer_iq(const uint8_t* cu8, size_t bytes, uint32_t sample_rate) noexcept;

}  // namespace orcsdr::dv_survey

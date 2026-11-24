#pragma once
// Digital Voice scan state machine. Pure logic: it decides WHEN to retune, hold, skip or stop and returns the frequency to
// tune; the receiver owner (radio session hooks) performs the tune and reports the result. It owns no radio, no USB and
// no decoder. The generic scan::Engine is a single-pass sweep with no hold, loop, skip or pause, so it cannot express
// scanner behaviour; this machine keeps its channel semantics (plan, settle, measure) and adds those.
//
//   idle -> tuning -> settling -> sampling -> (verified activity) holding -> (call ended / timeout) next channel
//                                          \-> (dwell elapsed, nothing verified) next channel
//   any active state -> paused (user) / failed (retune refused, receiver removed) ; idle on stop()
#include <cstddef>
#include <cstdint>

namespace orcsdr::dv {

enum class ScanState : uint8_t { idle, tuning, settling, sampling, holding, paused, failed };
enum class FailReason : uint8_t { none, no_plan, receiver_removed, retune_refused, unsupported };

constexpr size_t kMaxScanChannels = 16;            // saved channel list
constexpr size_t kMaxRangeChannels = 4096;          // a frequency range: channels are computed, not stored
constexpr uint32_t kMinStepHz = 6250;               // the narrowest digital voice raster (NXDN48)
enum class PlanKind : uint8_t { list, range };
constexpr uint32_t kMinChannelHz = 24000000u;     // RTL-SDR tuner floor; the driver's own caps apply on top
constexpr uint32_t kMaxChannelHz = 1766000000u;

struct ScanConfig {
  uint32_t settle_ms = 700;      // after an accepted tune: IQ from the old centre may still be in flight
  uint32_t dwell_ms = 1500;      // listening time per channel before moving on if nothing is verified
  uint32_t hold_ms = 4000;       // keep the channel this long after the last verified activity (short calls, hang time)
  uint32_t max_refusals = 3;     // consecutive refused tunes before failing
};

struct ScanInputs {
  bool receiver_ready = false;
  // Monotonic time of the newest VERIFIED event (integrity-valid or better) on the current channel, 0 = none since tune.
  uint32_t verified_ms = 0;
  // A sync-level candidate was seen on the current channel (lower confidence; extends the dwell once, never holds).
  bool candidate = false;
};

struct ScanStep {
  bool tune = false;
  uint32_t tune_hz = 0;
};

struct ScanStatus {
  ScanState state = ScanState::idle;
  FailReason fail = FailReason::none;
  size_t index = 0, count = 0;
  PlanKind kind = PlanKind::list;
  uint32_t range_start_hz = 0, range_step_hz = 0;
  uint32_t frequency_hz = 0;
  uint32_t passes = 0;              // completed trips through the plan
  uint32_t visited = 0;             // channels examined since start
  uint32_t holds = 0;               // holds triggered by verified activity
  bool manual_hold = false;
  uint32_t last_hold_reason_ms = 0; // timestamp of the verified activity that triggered the current/last hold
};

class Scanner {
 public:
  explicit Scanner(const ScanConfig& config = {}) noexcept : config_(config) {}
  // Validates every channel; false (and no state change) on an empty, oversized or out-of-range plan.
  bool set_plan(const uint32_t* channels_hz, size_t count) noexcept;
  // Frequency range: start + i * step for i in [0, count). Same validation and the same restrictions as set_plan().
  bool set_range(uint32_t start_hz, uint32_t step_hz, size_t count) noexcept;
  PlanKind plan_kind() const noexcept { return kind_; }
  void clear_plan() noexcept { if (!active()) { count_ = 0; index_ = 0; kind_ = PlanKind::list; } }
  size_t plan_count() const noexcept { return count_; }
  uint32_t plan_at(size_t i) const noexcept {
    if (i >= count_) return 0;
    return kind_ == PlanKind::list ? channels_[i] : range_start_ + static_cast<uint32_t>(i) * range_step_;
  }

  bool start(uint32_t now_ms) noexcept;     // false without a plan or when already running
  void stop() noexcept;                     // back to idle, plan kept
  void pause() noexcept;                    // keeps the current channel; no further tuning
  void resume(uint32_t now_ms) noexcept;    // listens on the current channel again
  void toggle_manual_hold() noexcept;       // while on: never leaves the channel; off: normal hold timing
  void skip(uint32_t now_ms) noexcept;      // go to the next channel now (any active state)
  void retune_result(bool accepted, uint32_t now_ms) noexcept;   // the answer to the last ScanStep::tune
  void stream_discontinuity(uint32_t now_ms) noexcept;           // IQ gap or reset: listen again on this channel

  ScanStep service(uint32_t now_ms, const ScanInputs& in) noexcept;
  ScanStatus status() const noexcept;
  bool active() const noexcept { return state_ != ScanState::idle && state_ != ScanState::failed; }
  static bool self_check() noexcept;

 private:
  void next_channel(uint32_t now_ms) noexcept;
  void begin_tune() noexcept { state_ = ScanState::tuning; tune_issued_ = false; }

  ScanConfig config_;
  uint32_t channels_[kMaxScanChannels]{};
  size_t count_ = 0, index_ = 0;
  PlanKind kind_ = PlanKind::list;
  uint32_t range_start_ = 0, range_step_ = 0;
  ScanState state_ = ScanState::idle;
  FailReason fail_ = FailReason::none;
  uint32_t state_since_ms_ = 0, hold_until_ms_ = 0, hold_reason_ms_ = 0, tune_ms_ = 0;
  uint32_t refusals_ = 0, passes_ = 0, visited_ = 0, holds_ = 0;
  bool tune_issued_ = false, manual_hold_ = false, extended_ = false;
  ScanState paused_from_ = ScanState::sampling;
};

}  // namespace orcsdr::dv

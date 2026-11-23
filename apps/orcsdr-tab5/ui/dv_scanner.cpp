#include "dv_scanner.hpp"

namespace orcsdr::dv {

bool Scanner::set_plan(const uint32_t* channels_hz, size_t count) noexcept {
  if (channels_hz == nullptr || count == 0 || count > kMaxScanChannels || active()) return false;
  for (size_t i = 0; i < count; ++i)
    if (channels_hz[i] < kMinChannelHz || channels_hz[i] > kMaxChannelHz) return false;
  for (size_t i = 0; i < count; ++i) channels_[i] = channels_hz[i];
  count_ = count;
  index_ = 0;
  return true;
}

bool Scanner::start(uint32_t now_ms) noexcept {
  if (count_ == 0) {
    state_ = ScanState::failed;
    fail_ = FailReason::no_plan;
    return false;
  }
  if (active()) return false;
  index_ = 0;
  passes_ = visited_ = holds_ = refusals_ = 0;
  manual_hold_ = false;
  fail_ = FailReason::none;
  state_since_ms_ = now_ms;
  begin_tune();
  return true;
}

void Scanner::stop() noexcept {
  state_ = ScanState::idle;
  fail_ = FailReason::none;
  manual_hold_ = false;
  tune_issued_ = false;
}

void Scanner::pause() noexcept {
  if (!active() || state_ == ScanState::paused) return;
  paused_from_ = state_ == ScanState::holding ? ScanState::holding : ScanState::sampling;
  state_ = ScanState::paused;
}

void Scanner::resume(uint32_t now_ms) noexcept {
  if (state_ != ScanState::paused) return;
  state_ = ScanState::sampling;   // listen afresh: pre-pause state is not trusted
  state_since_ms_ = now_ms;
  extended_ = false;
}

void Scanner::toggle_manual_hold() noexcept {
  if (!active()) return;
  manual_hold_ = !manual_hold_;
}

void Scanner::next_channel(uint32_t now_ms) noexcept {
  ++visited_;
  if (++index_ >= count_) {
    index_ = 0;
    ++passes_;
  }
  manual_hold_ = false;
  extended_ = false;
  state_since_ms_ = now_ms;
  if (count_ == 1) {          // single channel: no retune needed, listen again
    state_ = ScanState::sampling;
    return;
  }
  begin_tune();
}

void Scanner::skip(uint32_t now_ms) noexcept {
  if (!active() || state_ == ScanState::paused) return;
  next_channel(now_ms);
}

void Scanner::retune_result(bool accepted, uint32_t now_ms) noexcept {
  if (state_ != ScanState::tuning || !tune_issued_) return;
  if (accepted) {
    refusals_ = 0;
    state_ = ScanState::settling;
    state_since_ms_ = now_ms;
    tune_ms_ = now_ms;
    return;
  }
  if (++refusals_ >= config_.max_refusals) {
    state_ = ScanState::failed;
    fail_ = FailReason::retune_refused;
    return;
  }
  tune_issued_ = false;   // try again on the next service
}

void Scanner::stream_discontinuity(uint32_t now_ms) noexcept {
  if (state_ == ScanState::sampling || state_ == ScanState::holding) {
    state_ = ScanState::sampling;   // decoder state was reset by the receiver: examine the channel again
    state_since_ms_ = now_ms;
    extended_ = false;
  }
}

ScanStep Scanner::service(uint32_t now_ms, const ScanInputs& in) noexcept {
  ScanStep step;
  if (!active()) return step;
  if (!in.receiver_ready) {
    state_ = ScanState::failed;
    fail_ = FailReason::receiver_removed;
    return step;
  }
  switch (state_) {
    case ScanState::tuning:
      if (!tune_issued_) {
        tune_issued_ = true;
        step.tune = true;
        step.tune_hz = channels_[index_];
      }
      break;
    case ScanState::settling:
      if (static_cast<int32_t>(now_ms - state_since_ms_) >= static_cast<int32_t>(config_.settle_ms)) {
        state_ = ScanState::sampling;
        state_since_ms_ = now_ms;
        extended_ = false;
      }
      break;
    case ScanState::sampling:
      if (in.verified_ms != 0 && in.verified_ms >= tune_ms_) {
        state_ = ScanState::holding;
        hold_reason_ms_ = in.verified_ms;
        hold_until_ms_ = in.verified_ms + config_.hold_ms;
        ++holds_;
        break;
      }
      {
        uint32_t limit = config_.dwell_ms;
        if (in.candidate && !extended_) {   // a lower-confidence candidate earns one extra dwell, never a hold
          extended_ = true;
        }
        if (extended_) limit = config_.dwell_ms * 2;
        if (!manual_hold_ && static_cast<int32_t>(now_ms - state_since_ms_) >= static_cast<int32_t>(limit))
          next_channel(now_ms);
      }
      break;
    case ScanState::holding:
      if (in.verified_ms != 0 && in.verified_ms > hold_reason_ms_) {   // newer verified frame extends the hold
        hold_reason_ms_ = in.verified_ms;
        hold_until_ms_ = in.verified_ms + config_.hold_ms;
      }
      if (!manual_hold_ && static_cast<int32_t>(now_ms - hold_until_ms_) >= 0) next_channel(now_ms);
      break;
    default:
      break;
  }
  return step;
}

ScanStatus Scanner::status() const noexcept {
  ScanStatus s;
  s.state = state_;
  s.fail = fail_;
  s.index = index_;
  s.count = count_;
  s.frequency_hz = count_ ? channels_[index_] : 0;
  s.passes = passes_;
  s.visited = visited_;
  s.holds = holds_;
  s.manual_hold = manual_hold_;
  s.last_hold_reason_ms = hold_reason_ms_;
  return s;
}

bool Scanner::self_check() noexcept {
  Scanner s;
  const uint32_t plan[] = {460000000u, 461000000u};
  if (s.start(0) || s.state_ != ScanState::failed) return false;      // no plan
  s.stop();
  const uint32_t bad[] = {1000u};
  if (s.set_plan(bad, 1) || !s.set_plan(plan, 2)) return false;
  if (!s.start(0)) return false;
  ScanInputs in;
  in.receiver_ready = true;
  ScanStep st = s.service(0, in);
  if (!st.tune || st.tune_hz != plan[0]) return false;
  s.retune_result(true, 0);
  s.service(699, in);
  if (s.status().state != ScanState::settling) return false;
  s.service(700, in);
  if (s.status().state != ScanState::sampling) return false;
  in.verified_ms = 800;
  s.service(800, in);
  return s.status().state == ScanState::holding;
}

}  // namespace orcsdr::dv

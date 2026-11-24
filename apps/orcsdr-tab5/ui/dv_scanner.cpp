#include "dv_scanner.hpp"

namespace orcsdr::dv {

bool Scanner::set_range(uint32_t start_hz, uint32_t step_hz, size_t count) noexcept {
  const Segment seg{start_hz, step_hz, static_cast<uint32_t>(count)};
  return set_ranges(&seg, 1);
}

bool Scanner::set_ranges(const Segment* segments, size_t n) noexcept {
  if (active() || segments == nullptr || n == 0 || n > kMaxSegments) return false;
  uint64_t total = 0;
  for (size_t i = 0; i < n; ++i) {
    const Segment& g = segments[i];
    if (g.count == 0 || g.step_hz < kMinStepHz) return false;
    const uint64_t last = static_cast<uint64_t>(g.start_hz) + static_cast<uint64_t>(g.step_hz) * (g.count - 1);
    if (g.start_hz < kMinChannelHz || last > kMaxChannelHz) return false;
    total += g.count;
  }
  if (total > kMaxRangeChannels) return false;
  for (size_t i = 0; i < n; ++i) segs_[i] = segments[i];
  nseg_ = n;
  count_ = static_cast<size_t>(total);
  kind_ = PlanKind::range;
  index_ = 0; state_ = ScanState::idle; fail_ = FailReason::none;
  return true;
}

bool Scanner::set_plan(const uint32_t* channels_hz, size_t count) noexcept {
  if (channels_hz == nullptr || count == 0 || count > kMaxScanChannels || active()) return false;
  for (size_t i = 0; i < count; ++i)
    if (channels_hz[i] < kMinChannelHz || channels_hz[i] > kMaxChannelHz) return false;
  for (size_t i = 0; i < count; ++i) channels_[i] = channels_hz[i];
  count_ = count;
  kind_ = PlanKind::list;
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
  passes_ = visited_ = holds_ = refusals_ = control_releases_ = quick_skips_ = 0;
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
        step.tune_hz = plan_at(index_);
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
        hold_begin_ms_ = now_ms;
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
        // Wide scans: a channel with no sync-level sign of life is left early (never while a candidate is pending or the user holds).
        if (kind_ == PlanKind::range && config_.quick_ms && !in.candidate && !extended_ && limit > config_.quick_ms) limit = config_.quick_ms;
        if (limit < config_.dwell_ms && !manual_hold_ && static_cast<int32_t>(now_ms - state_since_ms_) >= static_cast<int32_t>(limit)) ++quick_skips_;
        if (!manual_hold_ && static_cast<int32_t>(now_ms - state_since_ms_) >= static_cast<int32_t>(limit))
          next_channel(now_ms);
      }
      break;
    case ScanState::holding:
      if (in.verified_ms != 0 && in.verified_ms > hold_reason_ms_) {   // newer verified frame extends the hold
        hold_reason_ms_ = in.verified_ms;
        hold_until_ms_ = in.verified_ms + config_.hold_ms;
      }
      if (!manual_hold_ && static_cast<int32_t>(now_ms - hold_until_ms_) >= 0) { next_channel(now_ms); break; }
      // Control-only channel: verified events keep renewing the hold, but no identity and no voice ever arrived. Move on.
      if (!manual_hold_ && config_.control_hold_max_ms &&
          static_cast<int32_t>(now_ms - hold_begin_ms_) >= static_cast<int32_t>(config_.control_hold_max_ms) &&
          (in.identity_ms == 0 || static_cast<int32_t>(in.identity_ms - hold_begin_ms_) < 0)) {
        ++control_releases_;
        next_channel(now_ms);
      }
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
  s.frequency_hz = count_ ? plan_at(index_) : 0;
  s.kind = kind_;
  if (kind_ == PlanKind::range && nseg_) {
    s.range_start_hz = segs_[0].start_hz;
    s.range_step_hz = segs_[0].step_hz;
    const Segment& l = segs_[nseg_ - 1];
    s.range_end_hz = l.start_hz + (l.count - 1) * l.step_hz;
    s.segments = static_cast<uint8_t>(nseg_);
  }
  s.control_releases = control_releases_;
  s.quick_skips = quick_skips_;
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
  if (s.status().state != ScanState::holding) return false;
  {   // frequency range: computed channels, validated like a list
    Scanner r;
    if (r.set_range(100u, 12500u, 10) || r.set_range(150000000u, 1000u, 10) || r.set_range(150000000u, 12500u, 0) ||
        r.set_range(150000000u, 12500u, kMaxRangeChannels + 1) || r.set_range(1765990000u, 12500u, 10))
      return false;
    if (!r.set_range(150000000u, 12500u, 100) || r.plan_kind() != PlanKind::range || r.plan_count() != 100 ||
        r.plan_at(0) != 150000000u || r.plan_at(99) != 150000000u + 99u * 12500u || r.plan_at(100) != 0)
      return false;
    if (!r.start(0)) return false;
    ScanInputs ri;
    ri.receiver_ready = true;
    const ScanStep rs = r.service(0, ri);
    if (!rs.tune || rs.tune_hz != 150000000u || r.status().kind != PlanKind::range || r.status().range_step_hz != 12500u) return false;
    r.stop();
    if (!r.set_plan(plan, 2) || r.plan_kind() != PlanKind::list) return false;     // a list replaces a range
  }
  return true;
}

}  // namespace orcsdr::dv

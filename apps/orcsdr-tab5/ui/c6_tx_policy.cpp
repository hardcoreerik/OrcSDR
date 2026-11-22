#include "c6_tx_policy.hpp"

namespace orcsdr::c6_tx_policy {

bool on_link_failure(State& s, uint32_t now_ms) {
  ++s.supply_restarts;
  s.stable_since_ms = 0;                   // the stable period starts again once the link is back
  // A failure soon after a step up means that power level does not hold on this supply: wait twice as long before the
  // next step up (10, 20, 40 ... minutes, at most kStepUpMaxMs), so a supply that stays bad settles at a level that holds
  // instead of losing the link every 10 minutes.
  if (s.last_step_up_ms != 0 && now_ms - s.last_step_up_ms < s.step_up_ms) {
    s.step_up_ms = s.step_up_ms >= kStepUpMaxMs / 2 ? kStepUpMaxMs : s.step_up_ms * 2;
  }
  s.last_step_up_ms = 0;
  if (s.level + 1 >= kLevels) return false;
  ++s.level;
  return true;
}

bool on_c6_restart(State& s, int32_t reset_reason, uint32_t now_ms) {
  if (reset_reason != kResetPowerOn && reset_reason != kResetBrownout && reset_reason != kResetPowerGlitch) return false;
  return on_link_failure(s, now_ms);
}

bool service(State& s, bool link_up, uint32_t now_ms) {
  if (!link_up) {
    s.stable_since_ms = 0;
    return false;
  }
  if (s.stable_since_ms == 0) {
    s.stable_since_ms = now_ms ? now_ms : 1;   // 0 means "not counting"
    return false;
  }
  const uint32_t stable_ms = now_ms - s.stable_since_ms;
  if (s.level == 0) {
    // At full power and stable for the longest wait: the supply is good again, so forget the back-off.
    if (s.step_up_ms != kStepUpMs && stable_ms >= kStepUpMaxMs) s.step_up_ms = kStepUpMs;
    return false;
  }
  if (stable_ms < s.step_up_ms) return false;
  --s.level;
  s.stable_since_ms = now_ms ? now_ms : 1;     // one level per stable period
  s.last_step_up_ms = now_ms ? now_ms : 1;
  return true;
}

}  // namespace orcsdr::c6_tx_policy

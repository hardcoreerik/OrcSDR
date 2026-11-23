#include "dead_air_control.hpp"

namespace orcsdr::dead_air {

void ControlRun::start(uint32_t now_ms, uint32_t duration_ms) {
  for (int i = 0; i < 3; ++i) {
    det_[i].reset();
    det_[i].set_sensitivity(static_cast<Sensitivity>(i));
  }
  started_ = running_ = true;
  complete_ = have_last_ = false;
  start_ms_ = end_ms_ = now_ms;
  duration_ms_ = duration_ms ? duration_ms : kDefaultMs;
  observed_ms_ = last_ms_ = 0;
}

void ControlRun::finish(bool complete) {
  running_ = false;
  complete_ = complete;
}

void ControlRun::stop(uint32_t now_ms) {
  if (!running_) return;
  end_ms_ = now_ms;
  finish(false);
}

void ControlRun::tick(uint32_t now_ms) {
  if (!running_) return;
  if (now_ms - start_ms_ >= duration_ms_) {
    end_ms_ = start_ms_ + duration_ms_;
    finish(true);
  }
}

void ControlRun::observe(const RfObservation& sample) {
  if (!running_) return;
  if (sample.monotonic_ms - start_ms_ >= duration_ms_) return;  // at or past the deadline: tick() ends the run
  const bool measured = sample.valid && sample.settled;
  if (measured) {
    // Only time between consecutive measured samples counts; a retune or a lost receiver is a gap, not quiet.
    if (have_last_) {
      const uint32_t delta = sample.monotonic_ms - last_ms_;
      if (delta <= 500u) observed_ms_ += delta;
    }
    last_ms_ = sample.monotonic_ms;
    have_last_ = true;
  } else {
    have_last_ = false;
  }
  for (EventModel& detector : det_) detector.observe_rf(sample);
}

uint32_t ControlRun::elapsed_ms(uint32_t now_ms) const {
  if (!started_) return 0;
  const uint32_t end = running_ ? now_ms : end_ms_;
  const uint32_t elapsed = end - start_ms_;
  return elapsed > duration_ms_ ? duration_ms_ : elapsed;
}

float ControlRun::per_hour(Sensitivity s) const {
  if (!rate_available()) return 0.0f;
  return static_cast<float>(count(s)) * 3600000.0f / static_cast<float>(observed_ms_);
}

}  // namespace orcsdr::dead_air

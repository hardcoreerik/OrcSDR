#pragma once
// Control run: the same RF detector, run on all three sensitivities at once, over a period the investigator declares
// to be "nothing there" (antenna removed, dummy load, an empty channel). Every event it counts is by definition a false
// alarm for that setup, so the result is a measured false-alarm rate per sensitivity, not a claim about the room.
//
// Pure logic, host-tested. The application feeds it the same RF observations the live detector sees, and suspends the
// live detector (and clip capture) while a run is in progress so the log never mixes control counts with findings.
#include <cstdint>

#include "dead_air_model.hpp"

namespace orcsdr::dead_air {

class ControlRun {
 public:
  static constexpr uint32_t kDefaultMs = 300000;  // five minutes
  static constexpr uint32_t kMinRateMs = 60000;   // observed time needed before a per-hour rate is meaningful

  void reset() { started_ = running_ = complete_ = have_last_ = false; observed_ms_ = 0; }  // forget any earlier run
  void start(uint32_t now_ms, uint32_t duration_ms = kDefaultMs);
  void stop(uint32_t now_ms);                     // end early; the counts so far are kept and marked incomplete
  void tick(uint32_t now_ms);                     // ends the run when its time is up
  void observe(const RfObservation& sample);      // ignored unless running

  bool running() const { return running_; }
  bool has_result() const { return started_ && !running_; }
  bool complete() const { return complete_; }     // ran its full duration
  uint32_t duration_ms() const { return duration_ms_; }
  uint32_t elapsed_ms(uint32_t now_ms) const;
  // Time during which a settled, valid level was actually measured; gaps are not counted as quiet.
  uint32_t observed_ms() const { return observed_ms_; }
  uint32_t count(Sensitivity s) const { return det_[static_cast<int>(s)].total(); }
  bool rate_available() const { return observed_ms_ >= kMinRateMs; }
  float per_hour(Sensitivity s) const;            // 0 unless rate_available()

 private:
  void finish(bool complete);
  EventModel det_[3];
  bool started_ = false, running_ = false, complete_ = false, have_last_ = false;
  uint32_t start_ms_ = 0, duration_ms_ = 0, observed_ms_ = 0, last_ms_ = 0, end_ms_ = 0;
};

}  // namespace orcsdr::dead_air

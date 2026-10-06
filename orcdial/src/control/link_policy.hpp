#pragma once
// Pure policy helpers for connection establishment. No RTOS, radio or crypto dependencies, so they
// are unit tested on the host (tests/policy_test.cpp) and shared by the Dial and the OrcSDR tablet.
#include <cstdint>

namespace orc::secure {

// Search order for a listener that does not know the peer's channel. A remembered channel is
// visited every second dwell and held long enough to span one peer retry interval, so a peer that
// stays on its usual channel is found in about two seconds instead of up to one full sweep.
class ChannelPlanner {
 public:
  static constexpr uint8_t kLastChannel = 11;
  static constexpr uint32_t kSweepDwellMs = 350;
  static constexpr uint32_t kPreferredDwellMs = 1500;
  void preferred(uint8_t channel) { preferred_ = (channel >= 1 && channel <= kLastChannel) ? channel : 0; }
  // Returns the next channel to listen on and how long to stay before asking again.
  uint8_t next(uint32_t& dwell_ms) {
    if (preferred_ && !on_preferred_) {
      on_preferred_ = true;
      dwell_ms = kPreferredDwellMs;
      return preferred_;
    }
    on_preferred_ = false;
    dwell_ms = kSweepDwellMs;
    do cursor_ = cursor_ >= kLastChannel ? 1 : uint8_t(cursor_ + 1); while (cursor_ == preferred_);
    return cursor_;
  }
 private:
  uint8_t preferred_ = 0, cursor_ = 0;
  bool on_preferred_ = false;
};

// Receiving one valid offer proves the peer is audible on this channel, not that it is listening
// here: a strong transmitter on channel 11 is also heard on channels 7 to 10. A lock therefore holds
// only while the peer acknowledges our unicast frames. With no evidence source (the tablet, whose
// channel is fixed by the router) the session's own lock is used as is.
class LockJudge {
 public:
  using Evidence = bool (*)();
  static constexpr uint32_t kGraceMs = 2500;
  bool locked(bool session_locked, uint32_t now, Evidence evidence) {
    if (session_locked != was_locked_) { was_locked_ = session_locked; since_ = now; }
    if (!session_locked) return false;
    return !evidence || uint32_t(now - since_) < kGraceMs || evidence();
  }
 private:
  bool was_locked_ = false;
  uint32_t since_ = 0;
};

// A connect attempt that times out (peer absent, wrong channel, lost frames) is retried a bounded
// number of times with increasing delay. Deliberate stops (Disconnect, Forget, Cancel, pairing)
// clear the intent so nothing is retried against the user's wishes.
class RetryScheduler {
 public:
  static constexpr uint8_t kMaxRetries = 6;
  void intent(bool connect) { intent_ = connect; retries_ = 0; armed_ = false; }
  // True when a new connect attempt should be started now.
  bool step(bool connected, bool timed_out, uint32_t now) {
    if (connected) { retries_ = 0; armed_ = false; return false; }
    if (!intent_ || !timed_out) { armed_ = false; return false; }
    if (!armed_) {
      if (retries_ >= kMaxRetries) return false;
      static const uint32_t backoff_ms[kMaxRetries] = {3000, 5000, 10000, 20000, 30000, 30000};
      due_ = now + backoff_ms[retries_];
      armed_ = true;
      return false;
    }
    if (int32_t(now - due_) < 0) return false;
    armed_ = false;
    ++retries_;
    return true;
  }
  uint8_t retries() const { return retries_; }
 private:
  bool intent_ = false, armed_ = false;
  uint8_t retries_ = 0;
  uint32_t due_ = 0;
};

}  // namespace orc::secure

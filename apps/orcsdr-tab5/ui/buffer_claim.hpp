#pragma once
// Rules for giving back a large capture buffer (the RDS multiplex capture, the audio recording) once nothing needs it.
//
// A capture buffer has three kinds of user: a writer on the DSP path, an exporter that copies it to the SD card, and the
// memory reclaim registry (memory_reclaim.hpp), which may ask for it back from a different task than the one that
// started the capture. Two things keep a release safe:
//   - Claim: a try-lock held while the buffer is allocated and armed, while it is exported, and while it is freed.
//     A release that cannot take the claim gives up at once and reports 0 bytes; it never waits.
//   - releasable(): the buffer may go only when no capture is running, no writer is inside it, and it holds nothing the
//     user has not saved yet. An unsaved capture is never thrown away to make room.
// Header only, no device headers: it builds on the host for tests.
#include <atomic>
#include <cstddef>

namespace orcsdr::buffer_claim {

class Claim {
 public:
  // True when the caller now owns the claim. A full barrier on both paths, so earlier writes by this task are visible
  // to the writer before the caller looks at the writer's flag.
  bool try_take() { return !held_.exchange(true, std::memory_order_seq_cst); }
  // For the start and export paths, which must run: waits (calling `wait` between tries) for a release to finish.
  void take(void (*wait)()) {
    while (!try_take()) {
      if (wait) wait();
    }
  }
  void give() { held_.store(false, std::memory_order_seq_cst); }
  bool held() const { return held_.load(std::memory_order_seq_cst); }

 private:
  std::atomic<bool> held_{false};
};

// Gives the claim back when it goes out of scope. Construct it only after the claim was taken.
class Hold {
 public:
  explicit Hold(Claim& claim) : claim_(claim) {}
  ~Hold() { claim_.give(); }
  Hold(const Hold&) = delete;
  Hold& operator=(const Hold&) = delete;

 private:
  Claim& claim_;
};

struct CaptureState {
  bool allocated = false;   // the buffer exists
  bool active = false;      // a capture is running
  bool writing = false;     // a writer is inside the buffer right now
  bool pending = false;     // an export has been requested and has not run yet
  size_t samples = 0;       // samples captured and still in the buffer
  bool saved = false;       // those samples are already in a file
};

constexpr bool releasable(const CaptureState& s) {
  return s.allocated && !s.active && !s.writing && !s.pending && (s.samples == 0 || s.saved);
}

}  // namespace orcsdr::buffer_claim

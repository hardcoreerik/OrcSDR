#pragma once
// DEVELOPMENT aid (SETUP toggle "SAVE DMR IQ (DEV)", off by default): keeps the last 12 s of the Digital Voice IQ stream
// (240 kS/s CU8, channel 60 kHz above the stream centre, i.e. the same layout as tests/digital_voice_corpus) in a PSRAM ring and, when
// something call-like is detected, lets 8 s more arrive and hands the 12 s (4 s before the trigger, 8 s after) to the UI task to
// write as /orcsdr/dviq_<epoch>_<channel kHz>.cu8. Purpose: real weak-signal recordings to tune the decoder on. Never audio, never TX.
// One save at a time, 60 s cooldown, at most 40 files per card visit. Host-portable.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dv_iq_rec {

constexpr size_t kBytesPerSec = 480000;                 // 240 kS/s x 2 bytes
constexpr uint32_t kRingSeconds = 12, kPostSeconds = 8, kCooldownMs = 60000, kMaxFiles = 40;
constexpr size_t kRingBytes = kBytesPerSec * kRingSeconds;   // 5.76 MB

struct Stats {
  uint32_t triggers = 0, saved = 0, failed = 0, skipped_busy = 0, skipped_cooldown = 0, skipped_limit = 0;
};

class Recorder {
public:
  ~Recorder() { end(); }
  bool begin();                         // allocate the ring (PSRAM on the Tab5); false without memory
  void end();                           // free it, unless a save is in flight (then freed by release())
  void set_enabled(bool on);
  bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
  // DSP task: append a live CU8 block. Dropped while a capture waits for the card.
  void feed(const uint8_t* cu8, size_t bytes);
  void reset();                         // IQ discontinuity: the pre-roll is no longer continuous
  // Any thread: something call-like was seen on `channel_hz`.
  void trigger(uint32_t now_ms, uint32_t channel_hz, uint32_t epoch);
  void service(uint32_t now_ms);        // any thread: closes the post-roll window
  // UI task: a finished capture. The buffer is made contiguous (oldest first) and stays valid until release().
  bool ready(const uint8_t** data, size_t* bytes, char* name, size_t name_size);
  void saving() { state_.store(kSaving, std::memory_order_release); }   // the card write was accepted
  void release(bool saved, uint32_t now_ms);
  const Stats& stats() const { return stats_; }
  bool busy() const { return state_.load() != kIdle; }

private:
  enum : uint8_t { kIdle, kArmed, kFrozen, kSaving };
  uint8_t* ring_ = nullptr;
  std::atomic<bool> enabled_{false};
  std::atomic<uint8_t> state_{kIdle};
  uint64_t wr_ = 0;                     // monotonic bytes written (DSP task)
  uint64_t gen_ = 0;
  uint32_t due_ms_ = 0, cooldown_until_ = 0, channel_hz_ = 0, epoch_ = 0, files_ = 0;
  size_t frozen_bytes_ = 0;
  bool free_pending_ = false, linear_ = false;
  Stats stats_{};
};

}  // namespace orcsdr::dv_iq_rec

#pragma once
// Optional recorder for DECODED DMR speech (SETUP page toggle "SAVE DMR SPEECH"). It sees only what the vocoder produced for a
// qualified, clear call: 8 kHz mono 16-bit PCM, silence frames and lost-superframe gaps as zeros, so the file is the call itself.
// Nothing else (no analogue audio, no tone test, no protected or unknown voice) can reach it. One call per file, up to 120 s.
// Host-portable. The card write is NOT done here: a finished recording waits in `ready()` for the UI task, which submits it
// through the shared background SD writer and reports back with release(). Receive only.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dv_speech_rec {

constexpr uint32_t kRateHz = 8000;
constexpr size_t kMaxSeconds = 120;
constexpr size_t kMaxSamples = kRateHz * kMaxSeconds;
constexpr size_t kHeaderBytes = 44;
constexpr uint32_t kMinSpeechMs = 400;      // shorter than this (or all silence) is not worth a file

struct Stats {
  uint32_t calls_saved = 0, calls_discarded = 0, calls_dropped_busy = 0, calls_truncated = 0;
  uint32_t last_samples = 0;
};

class Recorder {
public:
  ~Recorder() { end(); }
  bool begin();                       // allocate the buffer (PSRAM on the Tab5); false without memory
  void end();
  void set_enabled(bool on);          // turning it off discards a call in progress
  bool enabled() const { return enabled_; }
  // One decoded 20 ms frame of the voiced call (silent = a codec silence/concealed frame, written as zeros).
  void frame(uint8_t slot, uint32_t call_serial, uint32_t src, uint32_t dst, const int16_t pcm[160], bool silent, uint32_t now_ms);
  void gap(size_t frames);            // lost superframes: zeros
  void service(uint32_t now_ms);      // finalizes a call that has gone quiet for 2.5 s
  void flush();                       // finalize now (call end, DV closing)
  // UI side. ready() returns a finished recording; the buffer stays valid until release().
  bool ready(const int16_t** pcm, size_t* samples, char* name, size_t name_size, uint8_t header[kHeaderBytes]) const;
  void release(bool saved);   // the UI task is done with the buffer (saved = the card write succeeded)
  void set_clock(uint32_t epoch) { epoch_ = epoch; }   // wall clock seconds for the file name, 0 = unknown
  const Stats& stats() const { return stats_; }
  bool recording() const { return active_; }

private:
  void finalize(uint32_t now_ms);
  void start(uint8_t slot, uint32_t serial, uint32_t src, uint32_t dst);
  int16_t* buf_ = nullptr;
  bool enabled_ = false, active_ = false, any_speech_ = false;
  std::atomic<bool> pending_{false};      // set by the pump thread when a file is finished, cleared by the UI task
  size_t n_ = 0, speech_samples_ = 0;
  uint32_t serial_ = 0, src_ = 0, dst_ = 0, epoch_ = 0, last_ms_ = 0, seq_ = 0;
  uint8_t slot_ = 0;
  bool truncated_ = false, busy_counted_ = false;
  // finished recording waiting for the UI task
  size_t pend_n_ = 0;
  char pend_name_[72] = {};
  Stats stats_{};
};

}  // namespace orcsdr::dv_speech_rec

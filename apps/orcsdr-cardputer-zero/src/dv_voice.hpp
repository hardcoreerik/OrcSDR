#pragma once
// DMR voice playback: superframe -> de-interleave -> mbelib -> 48 kHz PCM.
//
// The audio device runs at 48 kHz, so the vocoder's 8 kHz output is interpolated
// 6x. Zero-order hold would work but leaves images audible on speech; linear
// interpolation costs a multiply and sounds markedly cleaner.
//
// Decoding happens on the radio thread (inside the d1 voice callback) and playing
// happens on the UI thread, so the PCM sits in a bounded ring between them. Writing
// to ALSA inside the callback would block the receiver whenever the buffer filled,
// which shows up as dropped IQ rather than as an audio glitch.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dv_ambe.hpp"

extern "C" {
#include "mbelib.h"
}

namespace orc::zero::dv {

class VoicePlayer {
 public:
  static constexpr int kVocoderRate = 8000;
  static constexpr int kOutputRate = 48000;
  static constexpr int kUpsample = kOutputRate / kVocoderRate;
  static constexpr int kFrameSamples = 160;
  // Four seconds of vocoder audio. A superframe is 360 ms, and a whole call is
  // decoded before playback starts in the diagnostic tool, so a one-second ring
  // silently discarded a third of a 9.4 s call.
  static constexpr size_t kRingCapacity = 4 * 48000;

  void reset() {
    cur_ = mbe_parms{};
    prev_ = mbe_parms{};
    prev_enh_ = mbe_parms{};
    ring_.clear();
    last_sample_ = 0;
    have_last_ = false;
    frames_decoded_ = 0;
    frames_dropped_ = 0;
  }

  // Called from the radio thread: 162 bytes = 18 frames of 9 bytes.
  void decode_superframe(const std::uint8_t* payload, size_t bytes) {
    const size_t frames = bytes / 9;
    for (size_t frame = 0; frame < frames; ++frame) {
      std::uint8_t bits[49];
      extract_ambe_bytes(payload + frame * 9, bits);

      char ambe_d[49];
      for (int i = 0; i < 49; ++i) ambe_d[i] = char(bits[i]);

      short pcm[kFrameSamples];
      int errs = 0, errs2 = 0;
      char err_str[64] = {0};
      std::memset(pcm, 0, sizeof(pcm));
      mbe_processAmbe2450Data(pcm, &errs, &errs2, err_str, ambe_d, &cur_, &prev_, &prev_enh_, 3);
      ++frames_decoded_;

      // Bounded: dropping the oldest audio is preferable to stalling the receiver.
      if (ring_.size() + kFrameSamples > kRingCapacity) {
        ring_.erase(ring_.begin(), ring_.begin() + std::ptrdiff_t(kFrameSamples));
        ++frames_dropped_;
      }
      ring_.insert(ring_.end(), pcm, pcm + kFrameSamples);
    }
  }

  // Called from the UI thread: interpolate the pending samples up to 48 kHz.
  // Returns how many output samples were written to `out`.
  size_t take_upsampled(std::int16_t* out, size_t capacity) {
    const size_t wanted = capacity / kUpsample;
    const size_t count = std::min(wanted, ring_.size());
    size_t written = 0;
    for (size_t n = 0; n < count; ++n) {
      const float current = float(ring_[n]);
      const float previous = have_last_ ? float(last_sample_) : current;
      for (int step = 0; step < kUpsample; ++step) {
        const float t = float(step) / float(kUpsample);
        const float value = previous + (current - previous) * (1.0f - t);
        out[written++] = std::int16_t(std::clamp(value, -32768.0f, 32767.0f));
      }
      last_sample_ = ring_[n];
      have_last_ = true;
    }
    if (count) ring_.erase(ring_.begin(), ring_.begin() + std::ptrdiff_t(count));
    return written;
  }

  size_t pending() const { return ring_.size(); }
  uint64_t frames_decoded() const { return frames_decoded_; }
  uint64_t frames_dropped() const { return frames_dropped_; }

 private:
  mbe_parms cur_{}, prev_{}, prev_enh_{};
  std::vector<std::int16_t> ring_;
  std::int16_t last_sample_ = 0;
  bool have_last_ = false;
  uint64_t frames_decoded_ = 0, frames_dropped_ = 0;
};

}  // namespace orc::zero::dv

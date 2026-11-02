#pragma once
// HFDL decoder skeleton (ESP32-P4 memory-first).
// Physical parameters from ICAO Annex 10 Vol III Ch 11 and AMCP/5 App D (REA-Results notes).
// Full bit-level (preamble, interleaver, exact SPDU layout) still needs Doc 9741 / ARINC 635.
// This layer produces MessageRecord for the dashboard model.

#include <cstddef>
#include <cstdint>

#include "../ui/hfdl_dashboard_model.hpp"

namespace orcsdr::hfdl {

// From research notes.
constexpr uint32_t kSymbolRate = 1800;          // symbols/s
constexpr uint32_t kCarrierOffsetHz = 1440;     // relative to SSB reference
constexpr uint32_t kFrameSeconds = 32;
constexpr uint32_t kSlotsPerFrame = 13;
constexpr uint32_t kSlotMs = (kFrameSeconds * 1000) / kSlotsPerFrame;  // ~2461 ms

enum class Rate : uint8_t { psk2_300 = 0, psk2_600, psk4_1200, psk8_1800, count };

struct Config {
  uint32_t sample_rate = 48000;   // typical audio / baseband rate
  uint32_t center_hz = 0;         // current tuned frequency
  Rate expected_rate = Rate::psk4_1200;
};

class Decoder {
 public:
  Decoder();
  void reset();
  void set_config(const Config& cfg);
  void set_model(hfdl_ui::Model* model);

  // Feed complex baseband or real audio samples. Returns true if a message was posted.
  bool process(const float* i, const float* q, size_t count);
  bool process_audio(const float* audio, size_t count);

  // Stats for the dashboard.
  uint32_t frames_seen() const { return frames_seen_; }
  uint32_t spdus_seen() const { return spdus_seen_; }
  uint32_t messages_posted() const { return messages_posted_; }

 private:
  bool try_sync_and_demod();
  void post_placeholder(const char* preview);

  Config cfg_{};
  hfdl_ui::Model* model_ = nullptr;
  uint32_t frames_seen_ = 0;
  uint32_t spdus_seen_ = 0;
  uint32_t messages_posted_ = 0;
  uint32_t sample_count_ = 0;
};

}  // namespace orcsdr::hfdl

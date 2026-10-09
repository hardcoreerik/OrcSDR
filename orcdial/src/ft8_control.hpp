#pragma once

#include <cstdint>

namespace orc::ft8_control {

enum class View : uint8_t {
  live = 0,
  decodes = 1,
  map = 2,
  hunter = 3,
  heard = 4,
  setup = 5,
  count = 6
};

enum class HunterCommand : int32_t {
  start_fast = 1,
  start_decode = 2,
  stop = 3,
  listen_best = 4
};

constexpr uint32_t kDecoderReady = 1u << 0;
constexpr uint32_t kClockReady = 1u << 1;
constexpr uint32_t kHunterSupported = 1u << 2;
constexpr uint32_t kHunterActive = 1u << 3;
constexpr uint32_t kHunterComplete = 1u << 4;
constexpr uint32_t kHunterDecodeMode = 1u << 5;
// Expert tuning (Tab5 FT8 Setup checkbox). kExpertEnabled: the checkbox is on, so a centre tap may switch to fine tuning.
// kExpertTuning: fine tuning is active right now (rotation steps Hz); the Dial shows the frequency large, the band small, and the step below.
constexpr uint32_t kExpertEnabled = 1u << 10;
constexpr uint32_t kExpertTuning = 1u << 6;
constexpr uint32_t kStepShift = 7;                  // bits 7..9: index into the Tune panel's step table
constexpr uint32_t kStepMask = 0x7u << kStepShift;
constexpr uint8_t kStepCount = 6;   // entries in the Tune panel's step table (the field has room for 8; 6 and 7 are not valid steps)
inline uint32_t with_step(uint32_t capabilities, uint8_t step_index) {
  if (step_index >= kStepCount) step_index = kStepCount - 1;   // never send an index the Dial cannot label
  return (capabilities & ~kStepMask) | ((uint32_t(step_index) & 0x7u) << kStepShift);
}
inline uint8_t step_index(uint32_t capabilities) { return uint8_t((capabilities & kStepMask) >> kStepShift); }
// Must match the Tab5 Tune panel's steps (apps/orcsdr-tab5/ui/ft8_tuning.hpp): 10 Hz, 100 Hz, 1 kHz, 5 kHz, 10 kHz, 100 kHz.
inline const char* step_label(uint8_t index) {
  static const char* const kLabels[kStepCount] = {"10 Hz", "100 Hz", "1 kHz", "5 kHz", "10 kHz", "100 kHz"};
  return index < kStepCount ? kLabels[index] : "--";
}

inline bool valid_view(uint8_t view) {
  return view < static_cast<uint8_t>(View::count);
}

inline const char* view_name(uint8_t view) {
  switch (static_cast<View>(view)) {
    case View::live: return "LIVE";
    case View::decodes: return "DECODES";
    case View::map: return "MAP";
    case View::hunter: return "HUNTER";
    case View::heard: return "HEARD";
    case View::setup: return "SETUP";
    case View::count: break;
  }
  return "VIEW --";
}

inline HunterCommand context_hunter_command(uint32_t capabilities) {
  if (capabilities & kHunterActive) return HunterCommand::stop;
  if (capabilities & kHunterComplete) return HunterCommand::listen_best;
  return HunterCommand::start_fast;
}

inline const char* hunter_state_name(uint32_t capabilities) {
  if (capabilities & kHunterActive) return "RUNNING";
  if (capabilities & kHunterComplete) return "COMPLETE";
  return capabilities & kHunterSupported ? "READY" : "--";
}

inline const char* hunter_press_name(uint32_t capabilities) {
  if (capabilities & kHunterActive) return "PRESS: STOP";
  if (capabilities & kHunterComplete) return "PRESS: BEST";
  return capabilities & kHunterSupported ? "PRESS: HUNT" : "HUNT --";
}

}  // namespace orc::ft8_control

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

#pragma once

#include "shortwave_model.hpp"

#include <cstdint>

namespace orcsdr::receiver_controls {

enum class Control : uint8_t {
  rf_gain,
  tuner_agc,
  rtl_agc,
  audio_boost,
  volume,
  offset_tuning,
  count,
};

enum class Availability : uint8_t { enabled, unavailable, hidden };

struct Capabilities {
  bool rf_gain = false;
  bool tuner_agc = false;
  bool rtl_agc = false;
  bool offset_tuning = false;
};

struct State {
  shortwave::ReceiverRoute route = shortwave::ReceiverRoute::unknown;
  Capabilities capabilities{};
  bool tuner_agc = true;
  bool rtl_agc = false;
  bool audio_boost = false;
  bool offset_tuning = false;
  uint8_t volume = 0;
  int16_t gain_tenth_db = 0;
};

struct Item {
  const char* label = "";
  const char* explanation = "";
  Availability availability = Availability::hidden;
  bool active = false;
  int32_t value = 0;
};

enum class ActionKind : uint8_t {
  none,
  gain_tenth_db,
  tuner_agc,
  rtl_agc,
  audio_boost,
  volume,
  offset_tuning,
};

struct Action {
  ActionKind kind = ActionKind::none;
  int32_t value = 0;
};

Item item(Control control, const State& state);
Action action(Control control, const State& state, int32_t value = 0);
bool self_check();

}  // namespace orcsdr::receiver_controls

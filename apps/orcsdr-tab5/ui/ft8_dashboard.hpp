#pragma once

#include "ft8_model.hpp"
#include "ft8_hunter.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8 {

enum class Tab : uint8_t { live, decodes, map, hunter, heard, setup, count };
enum class DecoderState : uint8_t { unbound, armed, listening, decoding, ready, error };
enum class ActionKind : uint8_t {
  none,
  home,
  tune_band,
  clear_decodes,
  start_hunt_fast,
  start_hunt_decode,
  stop_hunt,
  lock_hunter_best
};

struct Action {
  ActionKind kind = ActionKind::none;
  uint32_t value = 0;
};

struct Snapshot {
  uint64_t utc_ms = 0;
  bool clock_valid = false;
  bool receiver_running = false;
  bool gain_auto = true;
  int16_t gain_tenth_db = 0;
  int16_t battery_percent = -1;
  uint8_t candidate_count = 0;
  uint8_t last_slot_decodes = 0;
  size_t selected_band = 5;  // 20 m
  DecoderState decoder_state = DecoderState::unbound;
  HunterSnapshot hunter{};
  Decode decodes[kDecodeCapacity]{};
  size_t decode_count = 0;
};

void enter(const Snapshot& snapshot);
void update(const Snapshot& snapshot);
void draw();
Action handle_touch(int32_t x, int32_t y);
void leave();
bool active();
Tab tab();
void select_tab(Tab tab);   // from OrcDial; redraws when the tab changes
const Snapshot& snapshot();
bool dashboard_self_check();

}  // namespace orcsdr::ft8

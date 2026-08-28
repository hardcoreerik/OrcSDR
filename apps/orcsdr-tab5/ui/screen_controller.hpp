#pragma once

#include <cstdint>

namespace orcsdr::screens {

// Exactly one surface may own the framebuffer.  Radio/decoder work is not a
// surface and continues independently of this state.
enum class Id : uint8_t {
  none, home, fm, p25, adsb, lora, radio, visualizer, settings, documentation
};

struct Status {
  Id active = Id::none;
  Id return_to = Id::home;
  uint32_t transitions = 0;
  uint32_t rejected_draws = 0;
  uint32_t visible_updates = 0;
  uint32_t last_transition_ms = 0;
};

// Start a full-frame transition. The caller clears and renders the new static
// surface once, then calls finish_transition().
void begin_transition(Id next, uint32_t now_ms, bool remember_return = false);
void finish_transition();
bool transitioning();
Id close_settings(uint32_t now_ms);
bool owns(Id id);
// True while id is the selected surface, including its one-time transition draw.
bool is_active(Id id);
bool may_draw(Id id);
void note_visible_update(Id id);
const Status& status();
const char* name(Id id);
bool self_check();

}  // namespace orcsdr::screens

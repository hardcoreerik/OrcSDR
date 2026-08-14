#pragma once

#include <cstdint>

namespace orcsdr::audio_header {

enum class Action : uint8_t {
  none,
  opened,
  closed,
  volume_down,
  sound_toggle,
  volume_up,
};

struct Control {
  bool expanded = false;
  uint32_t hide_at_ms = 0;
};

void reset(Control& control);
void draw(const Control& control, uint8_t volume, bool sound_enabled,
          int32_t battery_percent);
Action handle_touch(Control& control, int32_t x, int32_t y, uint32_t now_ms);
bool service_timeout(Control& control, uint32_t now_ms);
bool self_check();

}  // namespace orcsdr::audio_header

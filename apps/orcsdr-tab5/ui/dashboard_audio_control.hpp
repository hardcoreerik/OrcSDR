#pragma once

#include <cstdint>

namespace orcsdr::audio_header {

enum class Action : uint8_t {
  none,
  opened,
  closed,
  volume_set,
  mute_toggle,
};

struct Control {
  bool expanded = false;
  uint8_t volume = 0;
  uint32_t hide_at_ms = 0;
};

void reset(Control& control);
void draw_badge();
void draw_brand(const char* subtitle);
void draw_battery(int32_t battery_percent);
void draw(const Control& control, uint8_t volume, bool sound_enabled,
          int32_t battery_percent);
void capture_volume_background();
void restore_volume_background();
void draw_home_button();
bool home_hit(int32_t x, int32_t y);
void draw_mute_button(bool sound_enabled);
bool mute_hit(int32_t x, int32_t y);
void draw_visualizer_button(bool enabled);
bool visualizer_hit(int32_t x, int32_t y);
void draw_settings_button();
bool settings_hit(int32_t x, int32_t y);
Action handle_touch(Control& control, int32_t x, int32_t y, uint32_t now_ms,
                    uint8_t current_volume);
bool service_timeout(Control& control, uint32_t now_ms);
bool self_check();

}  // namespace orcsdr::audio_header

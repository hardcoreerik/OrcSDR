#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::radio_ui {

struct ScopeGeometry {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct ScopeState {
  uint32_t frequency_hz = 0;
  uint32_t span_hz = 0;
  uint32_t filter_bandwidth_hz = 0;
  bool cb_channels = false;
  uint8_t cb_marker_channels[5]{};
};

struct Button {
  int width = 0;
  const char* text = "";
  uint32_t color = 0;
};

struct ControlLayout {
  int edge = 0;
  int band_y = 0;
  int tune_y = 0;
  int height = 0;
  int gap = 0;
};

enum class ControlAction : uint8_t {
  none,
  fm,
  am,
  wx,
  cb,
  lora,
  browse,
  toggle_audio_record,
  toggle_iq_record,
  toggle_capture,
  frequency_down,
  frequency_up,
  toggle_sound,
  volume_down,
  volume_up,
  toggle_graphics,
  cycle_lora_bandwidth,
  cycle_lora_spreading_factor,
};

uint16_t waterfall_color(float level);
void draw_grid(const ScopeGeometry& geometry);
void draw_axis(const ScopeGeometry& geometry, const ScopeState& state);
void draw_filter_edges(const ScopeGeometry& geometry, const ScopeState& state);
void draw_button_row(int x, int y, int height, int gap, const Button* buttons, size_t count);
int button_at(int x, int y, int height, int gap, int touch_x, int touch_y,
              const int* widths, size_t count);
ControlAction control_action(const ControlLayout& layout, bool lora, int touch_x,
                             int touch_y);
bool self_check();

}  // namespace orcsdr::radio_ui

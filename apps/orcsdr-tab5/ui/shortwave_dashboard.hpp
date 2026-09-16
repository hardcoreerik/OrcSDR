#pragma once

#include "receiver_tuning_controls.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::shortwave {

struct Snapshot {
  uint32_t frequency_hz = 7100000;
  uint32_t step_hz = 1000;
  uint32_t filter_bandwidth_hz = 6000;
  uint32_t span_hz = 480000;
  float relative_dbfs = -90.0f;
  float clipping_percent = 0.0f;
  bool running = false;
  bool driver_ready = false;
  bool sound_enabled = true;
  int32_t battery_percent = -1;
  char device[48]{};
  receiver_controls::State controls{};
  int gain_steps_tenth_db[32]{};
  uint8_t gain_step_count = 0;
};

enum class ActionKind : uint8_t {
  none,
  tune_hz,
  open_frequency,
  step_down,
  step_up,
  step_cycle,
  filter_cycle,
  sound_toggle,
  volume_down,
  volume_up,
  gain_auto,
  gain_tenth_db,
  rtl_agc,
  audio_boost,
  open_settings,
  exit_home,
};

struct Action {
  ActionKind kind = ActionKind::none;
  int32_t value = 0;
};

void enter(const Snapshot& snapshot);
void leave();
void draw();
void update(const Snapshot& snapshot);
void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                   float floor);
Action handle_touch(int32_t x, int32_t y);
Action handle_gain_drag(int32_t x, int32_t y);
bool active();
bool spectrum_active();
uint32_t saved_frequency();
void note_tuned(uint32_t frequency_hz);
bool dashboard_self_check();

}  // namespace orcsdr::shortwave

#pragma once

#include <cstddef>
#include <cstdint>

#include "p25_decoder.hpp"

namespace orcsdr::p25 {

enum class View : uint8_t { monitor, spectrum, talkgroups, program, rf_health, count };

struct Snapshot {
  uint32_t frequency_hz = 0;
  uint32_t span_hz = 960000;
  uint32_t effective_sps = 0;
  uint32_t target_sps = 960000;
  uint32_t usb_overruns = 0;
  uint32_t consumer_drops = 0;
  uint32_t audio_underruns = 0;
  uint32_t dsp_percent = 0;
  int32_t audio_ring_pressure_percent = -1;
  int32_t battery_percent = -1;
  float relative_dbfs = -90.0f;
  bool running = false;
  bool driver_ready = false;
  bool wifi_connected = false;
  bool sound_enabled = true;
  bool survey_active = false;
  bool hold = false;
  uint16_t hold_talkgroup = 0;
  bool auto_follow = true;
  bool encryption_skip = true;
  bool following_voice = false;
  uint32_t imbe_frames = 0;
  uint32_t imbe_errors = 0;
  uint8_t candidate_index = 0;
  uint8_t candidate_count = 0;
  uint8_t volume = 0;
  float candidate_levels[4] = {-120.0f, -120.0f, -120.0f, -120.0f};
  p25decoder::Snapshot decoded{};
  char last_error[32]{};
};

enum class ActionKind : uint8_t {
  none,
  tune_hz,
  previous_candidate,
  next_candidate,
  survey_toggle,
  hold_toggle,
  hold_talkgroup,
  skip_talkgroup,
  auto_follow_toggle,
  encryption_skip_toggle,
  span_down,
  span_up,
  sound_toggle,
  volume_down,
  volume_up,
  open_device_settings,
  exit_to_home,
};

struct Action {
  ActionKind kind = ActionKind::none;
  uint32_t value = 0;
};

void enter(const Snapshot& snapshot);
void leave();
void draw();
void update(const Snapshot& snapshot);
void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, float floor);
Action handle_touch(int32_t x, int32_t y);
bool active();
bool spectrum_active();
View view();
void show_documentation_view(View view, const Snapshot& snapshot,
                             bool show_volume_tray = false);
bool self_check();

}  // namespace orcsdr::p25

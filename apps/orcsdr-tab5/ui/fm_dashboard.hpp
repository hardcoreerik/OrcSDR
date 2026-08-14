#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::fm {

enum class View : uint8_t { listen, spectrum, station_rds, rf_health, settings, count };

struct Snapshot {
  uint32_t frequency_hz = 0;
  uint32_t step_hz = 100000;
  uint32_t filter_bandwidth_hz = 180000;
  uint32_t span_hz = 2000000;
  uint32_t effective_sps = 0;
  uint32_t target_sps = 960000;
  uint32_t usb_overruns = 0;
  uint32_t consumer_drops = 0;
  uint32_t audio_underruns = 0;
  uint32_t dsp_percent = 0;
  int32_t audio_ring_pressure_percent = -1;
  int32_t battery_percent = -1;
  float relative_dbfs = -90.0f;
  float left_dbfs = -90.0f;
  float right_dbfs = -90.0f;
  bool running = false;
  bool driver_ready = false;
  bool stereo = false;
  bool rds_carrier = false;
  bool rds_locked = false;
  bool wifi_connected = false;
  bool sound_enabled = true;
  bool graphics_enabled = true;
  bool recording = false;
  bool preset_scanning = false;
  uint8_t volume = 0;
  uint8_t preset_index = 0;
  uint8_t preset_count = 0;
  char program_service[9]{};
  char radio_text[65]{};
  char pi_code[5]{};
  char program_type[17]{};
  char last_error[32]{};
};

enum class ActionKind : uint8_t {
  none,
  tune_hz,
  step_down,
  step_up,
  seek_down,
  seek_up,
  save_preset,
  step_cycle,
  filter_down,
  filter_up,
  span_down,
  span_up,
  sound_toggle,
  volume_down,
  volume_up,
  graphics_toggle,
  recording_toggle,
  scan_presets,
  open_device_settings,
  exit_to_browse,
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
bool self_check();

}  // namespace orcsdr::fm

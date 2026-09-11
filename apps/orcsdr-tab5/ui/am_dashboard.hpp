#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr { class NvsStore; }

namespace orcsdr::am {

enum class View : uint8_t { listen, finder, spectrum, settings, count };

struct Snapshot {
  uint32_t frequency_hz = 1000000;
  uint32_t step_hz = 10000;
  uint32_t filter_bandwidth_hz = 10000;
  uint32_t span_hz = 480000;
  uint32_t presets_hz[6]{};
  float relative_dbfs = -90.0f;
  bool running = false;
  bool driver_ready = false;
  bool sound_enabled = true;
  bool graphics_enabled = true;
  bool recording = false;
  uint8_t volume = 0;
  uint16_t preset_count = 0;
  uint8_t preset_page = 0;
  uint8_t preset_pages = 1;
  bool gain_auto = true;
  bool gain_auto_selecting = false;
  int gain_tenth_db = 0;
  int gain_steps_tenth_db[32]{};
  uint8_t gain_step_count = 0;
  bool scan_active = false;
  uint16_t scan_step = 0;
  uint16_t scan_total = 1;
  uint8_t scan_found = 0;
  uint32_t scan_frequency_hz = 0;
  int8_t selected_preset = -1;
  int32_t battery_percent = -1;
};

enum class ActionKind : uint8_t {
  none,
  tune_hz,
  step_down,
  step_up,
  step_cycle,
  scan_spacing_toggle,
  filter_bandwidth_hz,
  span_down,
  span_up,
  preset_recall,
  preset_save,
  preset_replace,
  preset_delete,
  sound_toggle,
  volume_down,
  volume_up,
  graphics_toggle,
  recording_toggle,
  gain_auto,
  gain_tenth_db,
  scan_toggle,
  open_device_settings,
  exit_home,
};

struct Action {
  ActionKind kind = ActionKind::none;
  uint32_t value = 0;
};

struct TouchResult {
  Action action{};
  bool consumed = false;
};

void enter(const Snapshot& snapshot);
void leave();
void draw();
void update(const Snapshot& snapshot);
void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, float floor);
Action handle_touch(int32_t x, int32_t y);
TouchResult handle_preset_touch(int32_t x, int32_t y, bool pressed, uint32_t now_ms);
Action handle_gain_drag(int32_t x, int32_t y);
Action handle_bandwidth_drag(int32_t x, int32_t y);
bool active();
bool spectrum_active();
View view();
void load(NvsStore& store);
uint32_t saved_frequency();
uint32_t tune_step();
uint32_t scan_spacing();
void note_tuned(uint32_t frequency_hz);
uint32_t cycle_tune_step();
uint32_t toggle_scan_spacing();
uint32_t preset(size_t index);
void save_current_preset();
bool replace_preset(size_t index);
bool delete_preset(size_t index);
uint8_t prepare_scan_results(uint32_t start_hz, uint32_t step_hz,
                             const float* levels, size_t count, float* baseline_dbfs);
void clear_scan_results();
bool scan_prompt_active();
constexpr float kAutoGainTargetDbfs = -24.0f;
bool auto_gain_should_advance(float level_dbfs, size_t step, size_t step_count);
void populate_presets(Snapshot& snapshot);
bool self_check();

}  // namespace orcsdr::am

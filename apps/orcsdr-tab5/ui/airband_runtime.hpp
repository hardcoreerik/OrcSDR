#pragma once

#include "airband_catalog.hpp"
#include "airband_dashboard.hpp"
#include "receiver_tuning_controls.hpp"

#include <cstdint>

namespace orcsdr::airband {

struct LiveState {
  uint32_t now_ms = 0;
  uint32_t frequency_hz = kGuardFrequencyHz;
  float signal_dbfs = -120.0f;
  bool receiver_running = false;
  bool sound_enabled = true;
  int32_t battery_percent = -1;
  bool location_configured = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  receiver_controls::State controls{};
  int16_t gain_steps_tenth_db[32]{};
  uint8_t gain_step_count = 0;
  storage::FileSystem* filesystem = nullptr;
};

struct Hooks {
  // Tune must either hot-retune the active Airband receiver or queue an
  // Airband start when it is not running.
  bool (*tune)(uint32_t frequency_hz) = nullptr;
  void (*show_home)() = nullptr;
  void (*open_radio_settings)() = nullptr;
  void (*open_location_settings)() = nullptr;
  // Applies a shared receiver-control action (RF gain, tuner AGC, RTL AGC) to the driver.
  bool (*apply_gain)(const receiver_controls::Action& action) = nullptr;
  // Applies the AM channel filter bandwidth (Hz) to the live receiver.
  void (*apply_filter)(uint32_t bandwidth_hz) = nullptr;
};

void configure(const Hooks& hooks);
void enter(const LiveState& live);
void leave();
void update(const LiveState& live);
void redraw();
void service(const LiveState& live);
void handle_touch(int32_t x, int32_t y, const LiveState& live);

bool active();
Tab tab();
bool audio_open(float signal_dbfs);
uint32_t default_frequency();
uint32_t filter_bandwidth_hz();
uint32_t manual_step(uint32_t frequency_hz, int direction);
const Settings& settings();

bool runtime_self_check();

}  // namespace orcsdr::airband

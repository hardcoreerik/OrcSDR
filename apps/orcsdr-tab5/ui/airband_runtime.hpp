#pragma once

#include "airband_catalog.hpp"
#include "airband_dashboard.hpp"

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
  storage::FileSystem* filesystem = nullptr;
};

struct Hooks {
  // Tune must either hot-retune the active Airband receiver or queue an
  // Airband start when it is not running.
  bool (*tune)(uint32_t frequency_hz) = nullptr;
  void (*show_home)() = nullptr;
  void (*open_radio_settings)() = nullptr;
  void (*open_location_settings)() = nullptr;
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
uint32_t manual_step(uint32_t frequency_hz, int direction);
const Settings& settings();

bool runtime_self_check();

}  // namespace orcsdr::airband

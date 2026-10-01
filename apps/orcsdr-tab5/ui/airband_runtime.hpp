#pragma once

#include "airband_catalog.hpp"
#include "airband_dashboard.hpp"
#include "receiver_tuning_controls.hpp"

#include <cstdint>

namespace orcsdr::airband {

struct LiveState {
  uint32_t now_ms = 0;
  uint32_t frequency_hz = kGuardFrequencyHz;
  // In-channel carrier level (mean envelope of the channel-filtered IQ) in dBFS.
  float channel_db = -120.0f;
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
  // Steps the shared spectrum span (direction -1 narrower, +1 wider).
  void (*scope_span_step)(int direction) = nullptr;
  // Sets the shared spectrum span for the scope; 0 restores whatever it was before.
  void (*scope_span_set)(uint32_t hz) = nullptr;
};

void configure(const Hooks& hooks);
void enter(const LiveState& live);
void leave();
void update(const LiveState& live);
void redraw();
void service(const LiveState& live);
void handle_touch(int32_t x, int32_t y, const LiveState& live);

// Serial/test control (RTL_UI ACTION AIRBAND <verb> [value]). Verbs mirror the touch actions:
// TUNE <hz>, UP, DOWN, GUARD, SCAN, HOLD, SKIP, SOURCE, SPACING, RADIUS, SQUELCH <-dBFS magnitude>,
// SQUELCH_UP, SQUELCH_DOWN, SETTLE, HANG_UP, HANG_DOWN, PRIORITY, RELOAD, CLEAR, GAIN_UP,
// GAIN_DOWN, AGC, RTLAGC, TAB <0-4>. Returns false for an unknown or unusable verb.
bool serial_action(const char* verb, bool has_value, uint32_t value, const LiveState& live);
// One-line machine-readable status (RTL_AIRBAND_STATUS ...).
size_t status_line(const LiveState& live, char* out, size_t capacity);

bool active();
// The SCOPE tab: live spectrum and waterfall from the shared spectrum pipeline.
bool spectrum_active();
void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, uint32_t span_hz);
Tab tab();
// True while the carrier-versus-noise-floor squelch is open. Safe to call from the DSP task.
bool audio_open();
uint32_t default_frequency();
uint32_t filter_bandwidth_hz();
uint32_t manual_step(uint32_t frequency_hz, int direction);
const Settings& settings();

bool runtime_self_check();

}  // namespace orcsdr::airband

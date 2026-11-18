#pragma once
// What the Shortwave dashboard's touches do to the receiver. Moved out of main.cpp's handle_shortwave_dashboard_action;
// the memory and logbook actions are in shortwave_library_actions. No device headers and no state: the receiver, the audio
// path and the screens are reached through Hooks (plain function pointers) and a few pointers to main.cpp's variables.
#include "action_io.hpp"
#include "shortwave_dashboard.hpp"
#include "shortwave_library_actions.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace orcsdr::shortwave_actions {

// Every pointer must be set.
struct Hooks {
  action_io::Io io;

  // main.cpp's own variables (the receiver tasks read and write them too).
  const uint32_t* ui_frequency_hz = nullptr;             // rtl_ui_frequency_hz
  uint32_t* shortwave_step_hz = nullptr;                 // rtl_shortwave_step_hz
  std::atomic<uint32_t>* filter_bandwidth_hz = nullptr;  // rtl_filter_bandwidth_hz
  std::atomic<uint32_t>* scope_span_hz = nullptr;        // rtl_scope_span_hz
  std::atomic<bool>* audio_boost = nullptr;              // rtl_shortwave_audio_boost
  std::atomic<bool>* hunt_requested = nullptr;           // shortwave_hunt_requested
  std::atomic<bool>* hunt_cancel_requested = nullptr;    // shortwave_hunt_cancel_requested
  std::atomic<uint8_t>* hunt_band_index = nullptr;       // shortwave_hunt_band_index
  uint32_t scope_span_min_hz = 0;                        // kRtlScopeSpanMinHz
  int volume_step = 0;                                   // kRtlVolumeStep

  // Tuning
  uint32_t (*clamp_frequency)(uint32_t hz) = nullptr;     // rtl_clamp_frequency(RtlBand::shortwave, hz)
  bool (*capture_running)() = nullptr;                    // rtl_capture_state == running (acquire)
  void (*request_hot_retune)(uint32_t hz) = nullptr;
  void (*queue_local_listen)(uint32_t hz) = nullptr;      // queue_local_rtl_listen(RtlBand::shortwave, hz)
  void (*note_tuned)(uint32_t hz) = nullptr;              // orcsdr::shortwave::note_tuned
  uint32_t (*step_frequency)(int direction) = nullptr;    // rtl_step_frequency(shortwave, rtl_ui_frequency_hz, direction)
  uint32_t (*next_tuning_step)(uint32_t step_hz) = nullptr;
  // Filter, span, spectrum
  void (*audio_reset_demod_filters)() = nullptr;
  void (*reset_spectrum_renderer)() = nullptr;
  uint32_t (*clamp_filter_hz)(uint32_t hz) = nullptr;     // rtl_clamp_filter_hz(RtlBand::shortwave, hz)
  uint32_t (*span_cap_hz)() = nullptr;
  // Sound
  bool (*audio_user_enabled)() = nullptr;
  void (*set_audio_user_enabled)(bool enabled) = nullptr;
  void (*adjust_volume)(int delta) = nullptr;
  // Tuner (all the g_rtl and driver calls). The gain hooks that return a text give the driver's error name.
  bool (*tuner_gain_ready)() = nullptr;                   // g_rtl != nullptr && rtl_tuner_gain_available(frequency)
  bool (*tuner_gain_is_auto)() = nullptr;                 // true when the mode reads AUTO or cannot be read
  const char* (*set_tuner_gain_mode)(bool automatic) = nullptr;
  void (*set_tuner_gain_manual)() = nullptr;
  const char* (*set_tuner_gain)(int tenth_db) = nullptr;
  bool (*rtl_agc_available)() = nullptr;                  // g_rtl != nullptr && the dongle has an RTL AGC
  const char* (*set_rtl_agc)(bool enabled) = nullptr;
  // Station hunt
  size_t (*hunt_band_count)() = nullptr;
  // Screens
  void (*open_radio_defaults_settings)() = nullptr;
  void (*leave_dashboard)() = nullptr;                    // orcsdr::shortwave::leave()
  void (*show_home)() = nullptr;
  void (*refresh_active_screen)() = nullptr;

  orcsdr::shortwave_library_actions::Hooks library;
};

// Runs one dashboard action. The screen is refreshed at the end, except for open_frequency, open_settings and exit_home.
void handle(const orcsdr::shortwave::Action& action, const Hooks& hooks);

}  // namespace orcsdr::shortwave_actions

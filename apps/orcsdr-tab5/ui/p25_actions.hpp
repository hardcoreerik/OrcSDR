#pragma once
// What the P25 dashboard's touches do. Moved out of main.cpp's handle_p25_dashboard_action. No device headers and no
// state: the receiver, the follow state machine, the decoder and the SD card are reached through Hooks (plain function
// pointers) and pointers to main.cpp's P25 variables.
#include "action_io.hpp"
#include "p25_config.hpp"
#include "p25_dashboard.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace orcsdr::p25_actions {

// Every pointer must be set.
struct Hooks {
  action_io::Io io;

  // main.cpp's own variables (the receiver and decoder tasks read and write them too).
  std::atomic<bool>* survey_active = nullptr;             // p25_survey_active
  std::atomic<bool>* survey_cancel_requested = nullptr;   // p25_survey_cancel_requested
  std::atomic<bool>* hold = nullptr;                      // p25_hold
  std::atomic<bool>* auto_follow = nullptr;               // p25_auto_follow
  std::atomic<bool>* encryption_skip = nullptr;           // p25_encryption_skip
  uint16_t* hold_talkgroup = nullptr;                     // p25_hold_talkgroup
  uint16_t* skipped_talkgroup = nullptr;                  // p25_skipped_talkgroup
  uint32_t* skip_until_ms = nullptr;                      // p25_skip_until_ms
  uint32_t* voice_frequency_hz = nullptr;                 // p25_voice_frequency_hz
  const uint32_t* control_frequency_hz = nullptr;         // p25_control_frequency_hz
  std::atomic<uint32_t>* voice_session = nullptr;         // p25_voice_session
  uint8_t* candidate_index = nullptr;                     // p25_candidate_index
  const orcsdr::p25config::Config* config = nullptr;      // p25_config
  orcsdr::p25config::StoreState* profiles = nullptr;      // p25_profiles
  char* config_status = nullptr;                          // p25_config_status
  size_t config_status_size = 0;
  std::atomic<uint32_t>* scope_span_hz = nullptr;         // rtl_scope_span_hz
  uint32_t scope_span_min_hz = 0;                         // kRtlScopeSpanMinHz
  int volume_step = 0;                                    // kRtlVolumeStep

  // Tuning, survey and saved configuration
  void (*cancel_survey)() = nullptr;
  void (*start_survey)() = nullptr;
  void (*tune_control)(uint32_t hz) = nullptr;            // tune_p25_control
  void (*request_hot_retune)(uint32_t hz) = nullptr;
  void (*request_config_save)() = nullptr;
  void (*load_config)() = nullptr;                        // load_p25_config
  bool (*band_is_p25)() = nullptr;                        // rtl_ui_band == RtlBand::p25
  uint32_t (*millis_now)() = nullptr;
  // Follow state machine (control / voice / phase 2 probe)
  bool (*follow_is_voice)() = nullptr;
  void (*follow_to_control)() = nullptr;                  // p25_follow_state.store(control, release)
  void (*suspend_voice)() = nullptr;                      // p25decoder::suspend_voice()
  // The talkgroup of the voice call being followed, else of the decoder's current grant (0 when none). Reads the decoder
  // snapshot, so it stays out of this module.
  uint16_t (*current_talkgroup)() = nullptr;
  // The same, but 0 unless the grant is valid (what Hold pins).
  uint16_t (*grant_talkgroup_to_hold)() = nullptr;
  // Profiles on the SD card. The hooks that write return the library's result and fill `error`.
  bool (*sd_available)() = nullptr;                       // g_sd_fs != nullptr
  bool (*sd_ready)() = nullptr;                           // ensure_tab5_sd() && g_sd_fs != nullptr
  bool (*select_profile)(const char* id, char* error, size_t error_capacity) = nullptr;   // select, then apply the config
  bool (*import_profile)(const char* id, char* error, size_t error_capacity) = nullptr;
  bool (*export_profile)(const char* id, char* error, size_t error_capacity) = nullptr;
  bool (*take_profile_name)(char* name, size_t capacity) = nullptr;
  bool (*rename_profile)(const char* id, const char* name, char* error, size_t error_capacity) = nullptr;
  bool (*delete_profile)(const char* id, char* error, size_t error_capacity) = nullptr;
  // Receiver and screens
  uint32_t (*span_cap_hz)() = nullptr;
  void (*reset_spectrum_renderer)() = nullptr;
  bool (*audio_user_enabled)() = nullptr;
  void (*set_audio_user_enabled)(bool enabled) = nullptr;
  void (*adjust_volume)(int delta) = nullptr;
  void (*open_radio_defaults_settings)() = nullptr;
  void (*leave_dashboard)() = nullptr;                    // orcsdr::p25::leave()
  void (*show_home)() = nullptr;
  void (*refresh_active_screen)() = nullptr;
};

// Runs one dashboard action, then refreshes the screen.
void handle(const orcsdr::p25::Action& action, const Hooks& hooks);

}  // namespace orcsdr::p25_actions

#pragma once
// The RTL_P25 serial commands: IQ capture and replay, encryption and modulation, the two status reports, the
// Phase II trace switch, the SD profile store, CONFIG_RELOAD and SCAN.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device comes through Hooks, so the family builds and is tested on the host; it
// keeps no state of its own.
#include <cstddef>
#include <cstdint>

#include "p25_config.hpp"
#include "p25_decoder_core.hpp"

namespace orcsdr::serial_p25 {

// RTL_P25_IQ_STATUS: the shared IQ capture buffer as it is now.
struct IqStatus {
  const char* source = "none";    // which feature owns the capture
  bool active = false;
  bool ready = false;
  uint32_t bytes = 0;
  uint32_t max_bytes = 0;         // the P25 capture limit
  uint32_t frequency_hz = 0;
  uint32_t rate = 0;
  const char* last_path = "";     // empty when nothing was exported; printed as "none"
};

// RTL_P25_ENCRYPTION_STATUS. `last` is the algorithm id (high 16 bits) and key id (low 16 bits) last seen.
struct Encryption {
  uint32_t last = 0;
  bool detected = false;
  uint32_t muted_frames = 0;
  uint32_t returns = 0;
  bool skip_enabled = false;
};

enum class Follow : uint8_t { control, voice, phase2_probe };

// What RTL_P25_STATUS prints besides the decoder snapshot, the configuration and the profile store. The hook fills
// it in the order the dispatcher read these: USB metrics, encryption, follow state, then the rest top to bottom.
struct Status {
  uint32_t usb_overruns = 0;
  uint32_t usb_drops = 0;
  uint32_t encryption = 0;        // as Encryption::last
  Follow follow = Follow::control;
  uint32_t frequency_hz = 0;
  bool survey = false;
  uint32_t candidate = 0;
  bool hold = false;
  uint32_t hold_talkgroup = 0;
  bool auto_follow = false;
  bool encryption_skip = false;
  float relative_dbfs = 0.0f;
  uint32_t phase2_grant_events = 0;
  uint32_t phase2_sync_events = 0;
  uint32_t phase2_rejections = 0;
  bool phase2_trace = false;
  uint32_t grant_events = 0;
  uint32_t control_hz = 0;
  uint32_t voice_hz = 0;
  bool encrypted_detected = false;
  uint32_t encrypted_muted_frames = 0;
  uint32_t encrypted_returns = 0;
  uint32_t imbe_frames = 0;
  uint32_t imbe_errors = 0;
  uint32_t pcm_frames = 0;
  uint32_t voice_stack_hwm = 0;
  uint32_t imbe_max_us = 0;
  uint32_t imbe_synth_max_us = 0;
  uint32_t audio_queue_max_us = 0;
  uint32_t heap_free = 0;
  uint32_t heap_min = 0;
  uint32_t psram_free = 0;
  uint32_t iq_drops = 0;
  uint32_t audio_drops = 0;
};

// The trunking counters RTL_P25_PHASE2_STATUS prints besides the decoder snapshot.
struct Phase2Events {
  bool trace = false;
  uint32_t grants = 0;
  uint32_t syncs = 0;
  uint32_t rejections = 0;
};

// Every pointer must be set. The profile actions take the reply's error text buffer; they return false with it
// filled in (or left empty) when the SD card, the store or the operation fails.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)

  void (*iq_start)() = nullptr;                     // starts a P25 IQ capture; prints its own result
  bool (*iq_capture_is_p25)() = nullptr;
  void (*iq_stop_and_export)() = nullptr;           // forgets the SD state, stops and exports; prints its own result
  IqStatus (*iq_status)() = nullptr;
  void (*replay)(const char* path) = nullptr;       // replays a capture file; prints its own result

  Encryption (*encryption)() = nullptr;
  p25core::Snapshot (*snapshot)() = nullptr;        // the decoder's counters
  const p25config::Config* (*config)() = nullptr;   // the live configuration (never null)
  // Stores the choice, reconfigures the decoder, asks for the configuration to be saved and retunes the control
  // channel when P25 is on screen.
  void (*set_modulation)(p25core::Modulation modulation) = nullptr;

  Status (*status)() = nullptr;
  float (*candidate_level)(size_t index) = nullptr; // last measured level of a control-channel candidate, dBFS
  const p25core::Grant* (*phase2_probe_grant)() = nullptr;   // the grant being probed; null when not probing
  Phase2Events (*phase2_events)() = nullptr;
  bool (*phase2_trace)() = nullptr;
  void (*set_phase2_trace)(bool enabled) = nullptr;

  const p25config::StoreState* (*profiles)() = nullptr;      // the live profile store (never null)
  // Mounts the card and selects the profile; on success also applies it and retunes when P25 is on screen.
  bool (*profile_select)(const char* id, char* error, size_t error_size) = nullptr;
  bool (*profile_import)(const char* path, const char* id, char* error, size_t error_size) = nullptr;   // mounts
  bool (*profile_export)(const char* id, const char* path, char* error, size_t error_size) = nullptr;
  bool (*profile_rename)(const char* id, const char* name, char* error, size_t error_size) = nullptr;
  bool (*profile_delete)(const char* id, char* error, size_t error_size) = nullptr;
  void (*load_config)() = nullptr;                  // reloads the active profile from the card

  // Stops a survey, reloads, retunes and redraws the P25 screen; returns the configuration status text.
  const char* (*config_reload)() = nullptr;
  void (*scan)() = nullptr;                         // switches to P25 if needed and starts a survey unless one runs
};

// Returns true when `command` (not null) was an RTL_P25 command, whether it ran or was refused. RTL_P25_CONFIG_RELOAD
// and RTL_P25_SCAN from an unauthenticated caller are not taken: they return false and the dispatcher carries on.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_p25

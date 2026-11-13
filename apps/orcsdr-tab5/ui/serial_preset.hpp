#pragma once
// The RTL_PRESET_LIST and RTL_PRESET_TUNE serial commands (the FM presets found by RTL_PRESET_SCAN).
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication check and every reply are the ones the dispatcher had. The preset table
// and the tune and save actions are reached only through Hooks, and the module adds no frequency limit of its own.
// It keeps no state of its own.
#include <cstdint>

namespace orcsdr::serial_preset {

struct Preset {
  uint32_t freq_hz = 0;
  float level_dbfs = -120.0f;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  int (*preset_count)() = nullptr;                  // read again wherever the dispatcher read it again
  Preset (*preset)(int index) = nullptr;            // entry `index` (0-based), 0 <= index < preset_count()
  void (*tune_fm)(uint32_t frequency_hz) = nullptr;       // queues a local FM listen; the result is not used
  void (*persist_fm)(uint32_t frequency_hz) = nullptr;    // saves the FM frequency
};

// Returns true when `command` (not null) was one of these commands. RTL_PRESET_TUNE is matched only for an
// authenticated caller; otherwise it is not ours and falls through.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_preset

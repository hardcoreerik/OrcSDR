#pragma once
// The RTL_SIGNAL and RTL_PRESET_SCAN serial commands.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication check and every reply are the ones the dispatcher had. The measurements
// and the scan request are reached only through Hooks; the module keeps no state of its own.
#include <cstdint>

namespace orcsdr::serial_signal {

// The figures one RTL_SIGNAL line reports, already scaled the way the line prints them.
struct Signal {
  const char* band = "";
  uint32_t frequency_hz = 0;
  int signal_tenths = 0;     // smoothed signal level, dBFS x 10
  bool stereo_locked = false;
  int left_tenths = 0;
  int right_tenths = 0;
  bool rds_carrier = false;
  int rds_tenths = 0;
  int pilot_env_thou = 0;
  float clipping_percent = 0.0f;
  uint32_t filter_hz = 0;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  Signal (*signal)() = nullptr;                     // RTL_SIGNAL
  bool (*band_is_fm)() = nullptr;                   // RTL_PRESET_SCAN works on the FM band only
  void (*request_preset_scan)() = nullptr;          // sets the scan request flag
};

// Returns true when `command` (not null) was one of these commands. RTL_PRESET_SCAN is matched only for an
// authenticated caller; otherwise it is not ours and falls through.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_signal

#pragma once
// The RTL_DRIVER serial commands: RTL_DRIVER SELF_CHECK and RTL_DRIVER <action> (OFFSET, GAINMODE, GAIN, RTLAGC, BIAS,
// HFDIRECT, BW and TUNE). RTL_DRIVER STATUS is a separate route in main.cpp and is not part of this family.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication check, the argument parsing and every reply are the ones the dispatcher
// had. Each driver call is one hook that does the whole action (capability check included) and says whether the
// driver accepted it, so the order of the driver calls is the one the dispatcher had. The module keeps no state and
// takes every limit from Hooks, so it holds no tune range of its own.
#include <cstdint>

namespace orcsdr::serial_driver {

// What RTL_DRIVER SELF_CHECK reports.
struct SelfCheck {
  bool pass = false;
  const char* version = "";    // the driver version string
  unsigned profile = 0;        // the detected dongle profile
  unsigned device_caps = 0;
  unsigned required = 0;       // the capabilities that profile must have
};

// The driver's answer to one action: whether it was accepted and the driver's name for the result.
struct Outcome {
  bool accepted = false;
  const char* name = "";
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  SelfCheck (*self_check)() = nullptr;
  bool (*installed)() = nullptr;                    // a dongle driver is open
  Outcome (*offset_tuning)(bool enabled) = nullptr;
  Outcome (*gain_mode)(bool automatic) = nullptr;
  Outcome (*gain)(int gain_tenth_db) = nullptr;
  Outcome (*rtl_agc)(bool enabled) = nullptr;
  Outcome (*bias_tee)(bool enabled) = nullptr;
  Outcome (*hf_direct)(uint32_t override_hz) = nullptr;   // UINT32_MAX = automatic, 0 = off, else a minimum in Hz
  Outcome (*bandwidth)(uint32_t hz) = nullptr;
  Outcome (*retune)(uint32_t hz) = nullptr;
  uint32_t hf_direct_min_hz = 0;     // lowest HFDIRECT value accepted
  uint32_t hf_upconv_lo_hz = 0;      // HFDIRECT values must be below this
  uint32_t freq_min_hz = 0;          // TUNE range, both ends included
  uint32_t freq_max_hz = 0;
};

// Returns true when `command` (not null) was one of these RTL_DRIVER commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_driver

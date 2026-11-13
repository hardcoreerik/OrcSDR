#pragma once
// The RTL_GAIN, RTL_AM_GAIN and RTL_FM_GAIN serial commands: RTL_GAIN STATUS, RTL_GAIN <action>, RTL_AM_GAIN STATUS
// and RTL_FM_GAIN STATUS.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication check, the argument parsing and every reply are the ones the dispatcher
// had. Every gain change and every device read comes through Hooks, so the family builds and is tested on the host;
// it keeps no state of its own and holds no gain limits other than the 0..496 manual range the command always had.
namespace orcsdr::serial_gain {

// What RTL_AM_GAIN STATUS and RTL_FM_GAIN STATUS report for one band.
struct AutoStatus {
  bool automatic = false;      // the band's auto gain is on
  bool selecting = false;      // the auto gain is currently choosing a step
  int gain_tenth_db = 0;       // the tuner gain now (0 when no dongle is open)
  double target_dbfs = 0.0;    // the level auto gain aims for
  double clipping_percent = 0.0;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  void (*print_status)() = nullptr;                 // RTL_GAIN STATUS: prints the whole status reply itself
  void (*set_auto)() = nullptr;                     // AUTO and SMART: the same path as the Home gain popup
  void (*set_manual)(int gain_tenth_db) = nullptr;
  void (*set_rtl_agc)(bool enabled) = nullptr;
  int (*current_gain)() = nullptr;                  // the tuner gain now, 0 when there is no dongle or no reading
  void (*refresh_ui)() = nullptr;                   // redraw the receiver UI after a change
  AutoStatus (*am_status)() = nullptr;
  AutoStatus (*fm_status)() = nullptr;
};

// Returns true when `command` (not null) was one of these gain commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_gain

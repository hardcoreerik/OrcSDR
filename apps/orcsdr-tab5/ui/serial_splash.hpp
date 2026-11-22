#pragma once
// RTL_SPLASH_GATE [ON|OFF|STATUS]: whether the boot splash waits for the OrcSDR button (stored setting).
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The route,
// its matching (any line starting RTL_SPLASH_GATE), the sign-in rule (only changes need it) and every reply are the
// ones the dispatcher had. The stored setting comes through Hooks.
namespace orcsdr::serial_splash {

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  void (*store_gate)(bool on) = nullptr;            // saved setting "splash_gate"
  bool (*gate)() = nullptr;                         // saved setting, true when never set
};

// Returns true when `command` (not null) was this command, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_splash

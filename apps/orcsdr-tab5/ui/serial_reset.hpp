#pragma once
// RTL_RESET, RTL_RESET CPU, RTL_USB_SAFE_MODE_STATUS and RTL_USB_SAFE_MODE_RESET CONFIRM.
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The
// routes, their matching, the sign-in rule (all but the safe-mode status need it) and every reply are the ones the
// dispatcher had. The restarts themselves stay in main.cpp; the restart hooks do not return on hardware.
namespace orcsdr::serial_reset {

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  void (*restart_clean)() = nullptr;                // orcsdr_restart_clean(): the normal restart
  void (*restart_cpu)() = nullptr;                  // 25 ms pause, then esp_restart() (issue #127 diagnostic)
  bool (*usb_safe_mode_active)() = nullptr;
  void (*usb_safe_mode_reset_and_restart)() = nullptr;
};

// Returns true when `command` (not null) was one of these commands.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_reset

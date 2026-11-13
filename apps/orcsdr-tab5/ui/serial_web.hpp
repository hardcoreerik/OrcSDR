#pragma once
// The web console serial commands: RTL_WEB and RTL_WEB_STATUS, RTL_WEB_AUDIO_STATUS and RTL_WEB ON|OFF.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication check and every reply are the ones the dispatcher had. The web server
// itself is reached only through Hooks, so the family builds and is tested on the host; it keeps no state of its own.
namespace orcsdr::serial_web {

// What the RTL_WEB_STATUS and RTL_WEB_OK replies report.
struct Status {
  bool enabled = false;
  bool listening = false;
  char url[48] = {};   // empty when the server is not listening or Wi-Fi is not connected
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  Status (*status)() = nullptr;                     // RTL_WEB, RTL_WEB_STATUS
  void (*print_audio_status)() = nullptr;           // RTL_WEB_AUDIO_STATUS: prints the whole reply itself
  // RTL_WEB ON / OFF: saves and applies the setting, polls the server, and returns what it then reports.
  Status (*apply_enabled)(bool enable) = nullptr;
  void (*update_global_settings)() = nullptr;       // called after the reply; the hook does nothing unless global settings are active
};

// Returns true when `command` (not null) was one of these web commands. RTL_WEB ON and OFF are matched only for an
// authenticated caller; otherwise they are not ours and fall through.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_web

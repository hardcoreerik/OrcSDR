#pragma once
// The RTL_UI STATUS command: one line describing the screen that is showing and which dashboards are active. It
// does not need authentication.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// route and its reply are the ones the dispatcher had. Everything the line reports comes through the snapshot hook,
// so the family builds and is tested on the host; it keeps no state of its own.
#include <cstdint>

namespace orcsdr::serial_ui_status {

struct UiStatus {
  const char* screen = "";
  const char* band = "";
  uint32_t frequency_hz = 0;
  int settings = 0;
  int fm = 0;
  int am = 0;
  int p25 = 0;
  int adsb = 0;
  int lora = 0;
  int rf24 = 0;
  int home_font = 0;
  int graphics = 0;
  int radio_ui = 0;
  const char* route = "";
  uint32_t volume = 0;
  int audio_enabled = 0;
  int tray = 0;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  UiStatus (*ui_status)() = nullptr;                // reads the facts in the order the original reply listed them
};

// Returns true when `command` (not null) was this command.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_ui_status

#pragma once
// The on-screen navigation commands: RTL_UI TAP <x> <y> (a finger on the panel), RTL_UI OPEN <name> (jump to a
// dashboard) and RTL_RF24_PAGE <n> (pick a page of the 2.4 GHz dashboard). All three need authentication.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order and every reply are the ones the dispatcher had. The family parses the line and picks the
// target by name; the screens themselves are opened by main.cpp through the hooks, so this module includes no UI
// header and keeps no state of its own.
#include <cstdint>

namespace orcsdr::serial_ui_nav {

// What RTL_UI OPEN can open. main.cpp maps each one to the same show_/open_ call the dispatcher made.
enum class Target : uint8_t {
  home,
  clock,
  fm,
  am,
  shortwave,
  airband,
  cb,
  p25,
  adsb,
  lora,
  rf_lab,
  wifi_analysis,
  ft8,
  settings,
  weather,
  pocsag,
  acars,
  dead_air,
  ais,
  visualizer,
  marine,
  satellite,
  utilities,
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  void (*touch)(int x, int y) = nullptr;            // the same routing as a finger on the panel
  void (*open)(Target target) = nullptr;            // shows the home screen, the clock settings or the dashboard
  unsigned (*rf24_page_count)() = nullptr;          // number of pages of the 2.4 GHz dashboard
  bool (*rf24_select_page)(uint8_t page) = nullptr; // false when that dashboard is not active
};

// Returns true when `command` (not null) was one of these commands.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_ui_nav

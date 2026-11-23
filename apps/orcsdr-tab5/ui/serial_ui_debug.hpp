#pragma once
// The screen test-automation serial commands: UI_TAP <x> <y>, RTL_TAG_DOC ON|OFF, RTL_SCREEN_STATUS,
// RTL_UI_REGRESSION CHECK and RTL_UI_REGRESSION RUN.
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The routes,
// their order, the authentication checks and every reply are the ones the dispatcher had. Everything comes through
// Hooks, so this builds and is tested on the host and keeps no state.
#include <cstdint>

namespace orcsdr::serial_ui_debug {

struct ScreenStatus {   // what RTL_SCREEN_STATUS reports (screen_controller.hpp)
  const char* active = "";
  const char* return_to = "";
  uint32_t transitions = 0;
  uint32_t rejected_draws = 0;
  uint32_t visible_updates = 0;
  uint32_t last_transition_ms = 0;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  bool (*tap_busy)() = nullptr;                     // a virtual tap is still playing
  void (*start_tap)(int x, int y) = nullptr;        // one finger press through the real touch path
  void (*set_tag_doc)(bool on) = nullptr;           // tag screens in documentation captures (not saved)
  bool (*tag_doc)() = nullptr;
  ScreenStatus (*screen_status)() = nullptr;
  void (*run_ui_regression)(bool run) = nullptr;    // false: passive CHECK; true: the Home -> screen restore RUN
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_ui_debug

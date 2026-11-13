#pragma once
// The RTL_KEYBOARD serial commands: STATUS, KEY and ECHO.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device comes through Hooks, so the family builds and is tested on the host; it
// keeps no state of its own.
#include <cstdint>

#include "keyboard_input.hpp"
#include "tab5_keyboard.hpp"

namespace orcsdr::serial_keyboard {

// The control that has keyboard focus, as RTL_KEYBOARD STATUS reports it.
struct Focus {
  bool focused = false;    // a control has focus; the rectangle and `ring` mean something only then
  uint32_t controls = 0;   // focusable controls on the screen
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  bool ring = false;       // the focus ring is drawn
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  tab5_keyboard::Status (*status)() = nullptr;
  bool (*echo)() = nullptr;                         // the diagnostic key echo is on
  bool (*editor_active)() = nullptr;                // a text field is being edited
  Focus (*focus)() = nullptr;
  void (*route_key)(const keyboard_input::Key& key) = nullptr;   // delivers a key as if it had been typed
  bool (*set_echo)(bool on) = nullptr;              // switches the key echo and returns its state afterwards
};

// Returns true when `command` (not null) was an RTL_KEYBOARD command, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_keyboard

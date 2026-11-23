#pragma once
// The RTL_CB, RTL_FM_SCOPE and RTL_AIRBAND STATUS serial commands.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device comes through Hooks, so the family builds and is tested on the host; it
// keeps no state of its own.
#include <cstddef>
#include <cstdint>

namespace orcsdr::serial_scope {

// The FM spectrum view, as RTL_FM_SCOPE reports it.
struct FmScope {
  bool active = false;      // the spectrum view is showing
  uint32_t fps = 0;         // 0 when not showing
  uint32_t draw_ms = 0;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  void (*citizens_band)(const char* command) = nullptr;   // runs the whole RTL_CB command line
  FmScope (*fm_scope)() = nullptr;
  // Fills `line` (size bytes) with the airband status line; returns its length, 0 or less when there is none.
  int (*airband_status)(char* line, size_t size) = nullptr;
};

// Returns true when `command` (not null) was one of these commands.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_scope

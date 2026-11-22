#pragma once
// The RTL_SERIAL VERBOSITY serial commands: read the level, or set it (authenticated).
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device comes through Hooks, so the family builds and is tested on the host; it
// keeps no state of its own. A level travels as the small number the device stores for it.
#include <cstdint>

namespace orcsdr::serial_verbosity {

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  uint8_t (*current)() = nullptr;                   // the level in force
  const char* (*name)(uint8_t level) = nullptr;     // "QUIET", "NORMAL", "DEBUG" or "TRACE"
  bool (*parse)(const char* text, uint8_t* level) = nullptr;   // false when `text` names no level
  void (*save_and_apply)(uint8_t level) = nullptr;  // stores the setting, then applies it
};

// Returns true when `command` (not null) was an RTL_SERIAL VERBOSITY command, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_verbosity

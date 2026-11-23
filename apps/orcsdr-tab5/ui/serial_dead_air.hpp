#pragma once
// The RTL_DEAD_AIR serial commands: FLOOD, STATUS, VEIL (report and command) and ENV.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. The Dead Air
// feature itself is untouched: the family calls the same dead_air_app functions the dispatcher called, through
// Hooks, so it builds and is tested on the host; it keeps no state of its own.
#include <cstddef>
#include <cstdint>

namespace orcsdr::serial_dead_air {

// Every pointer must be set. The four text hooks fill `out` (at most `size` bytes, NUL included) with one line and
// no line ending.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  uint32_t (*flood_manual_marks)(uint32_t count) = nullptr;   // bench aid: logs manual marks, returns how many
  void (*describe)(char* out, size_t size) = nullptr;         // RTL_DEAD_AIR STATUS
  void (*describe_veil)(char* out, size_t size) = nullptr;    // RTL_DEAD_AIR VEIL
  // RTL_DEAD_AIR VEIL <args>: runs the command and writes its reply. The result is not used, as before the move.
  bool (*veil_command)(const char* args, char* reply, size_t size) = nullptr;
  void (*describe_env)(char* out, size_t size) = nullptr;     // RTL_DEAD_AIR ENV
};

// Returns true when `command` (not null) was an RTL_DEAD_AIR command, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_dead_air

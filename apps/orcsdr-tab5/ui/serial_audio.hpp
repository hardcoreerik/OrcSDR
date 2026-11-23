#pragma once
// The RTL_VOLUME and RTL_SOUND serial commands: RTL_VOLUME, RTL_SOUND and RTL_SOUND ON|OFF.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the replies and the order of the speaker calls are the ones the dispatcher had. Every device
// read and action comes through Hooks, so the family builds and is tested on the host; it keeps no state of its own.
#include <cstdint>

namespace orcsdr::serial_audio {

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  uint8_t (*volume)() = nullptr;                    // the receiver volume now
  bool (*sound_enabled)() = nullptr;                // the speaker is enabled
  void (*set_sound_enabled)(bool enable) = nullptr; // RTL_SOUND ON / OFF
};

// Returns true when `command` (not null) was one of these audio commands.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_audio

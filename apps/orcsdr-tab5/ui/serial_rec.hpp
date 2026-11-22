#pragma once
// The recording and tool serial commands: RTL_REC_START, RTL_REC_STOP, RTL_REC_STATUS, RTL_REC_SAVE, RTL_TOOL and
// RTL_TOOL <RADIO|SCOPE|CAPTURE>.
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The routes,
// their order, the (absent) authentication checks and every reply are the ones the dispatcher had. The recording
// itself is audio_rec.cpp, which prints its own replies; everything comes through Hooks, so this builds and is tested
// on the host and keeps no state.
#include <cstdint>

namespace orcsdr::serial_rec {

enum class Tool : uint8_t { radio = 0, scope = 1, capture = 2 };   // the firmware's OrcTool values

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  void (*rec_start)() = nullptr;                    // audio_rec::start
  void (*rec_stop)() = nullptr;                     // audio_rec::stop_and_export
  void (*rec_status)() = nullptr;                   // audio_rec::status_print
  void (*rec_save)() = nullptr;                     // forget the SD card state so it is tried again, then stop_and_export
  void (*set_tool)(Tool tool) = nullptr;            // set_orc_tool, which prints RTL_TOOL <name>
  const char* (*tool_name)() = nullptr;             // the current tool's name
};

// Returns true when `command` (not null) was one of these commands.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_rec

#pragma once
// The RF Lab and Visualizer serial commands: RTL_LAB ... and RTL_VIS ... .
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The routes,
// their order, the sign-in rule (a few read-only subcommands work without it) and every reply are the ones the
// dispatcher had. The subcommands themselves are parsed by rf_lab.cpp and rf_visualizer.cpp; this only gates them and
// handles their "open the screen" request. Everything comes through Hooks, so it builds and is tested on the host.
#include <cstddef>

namespace orcsdr::serial_lab_vis {

// Every pointer must be set.
struct Hooks {
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  // rf_lab::process_command / visualizer::process_command: false for an unknown subcommand.
  bool (*lab_command)(const char* command, char* response, size_t size) = nullptr;
  void (*open_lab)() = nullptr;
  bool (*lab_active)() = nullptr;
  bool (*vis_command)(const char* command, char* response, size_t size) = nullptr;
  void (*open_visualizer)() = nullptr;
  bool (*vis_active)() = nullptr;
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_lab_vis

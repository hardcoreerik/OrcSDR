#pragma once
// The JS8 and FT8 serial command prefixes: "JS8 ..." and "FT8 ..." (or the bare words) go to the JS8 and FT8 command
// parsers with the rest of the line.
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The routes,
// their order and their matching are the ones the dispatcher had: the word must be followed by a space or end the line.
// No sign-in check here, as before. The parsers stay where they are; they come in through Hooks.
namespace orcsdr::serial_digimodes {

// Every pointer must be set.
struct Hooks {
  void (*js8)(const char* args) = nullptr;   // process_js8_command: args start at the space (or are "")
  void (*ft8)(const char* args) = nullptr;   // process_ft8_command
};

// Returns true when `command` (not null) was one of these commands.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_digimodes

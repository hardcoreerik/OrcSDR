#pragma once
// The RTL_CATALOG serial commands: STATUS / LIST, CHECK / FETCH, INSTALL <id> and REMOVE <id> CONFIRM.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device comes through Hooks, so the family builds and is tested on the host; it keeps
// no state of its own.
#include "catalog_sync.hpp"

namespace orcsdr::serial_catalog {

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  catalog::State (*state)() = nullptr;              // a copy of the catalog state, read each time it is needed
  void (*check)() = nullptr;                        // queues the catalog check (the settings action catalog_check)
  void (*install)(int pack_index) = nullptr;        // queues the install of the pack in that catalog slot
  void (*remove)(int pack_index) = nullptr;         // queues the removal of the pack in that catalog slot
};

// Returns true when `command` (not null) was an RTL_CATALOG command, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_catalog

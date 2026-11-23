#pragma once
// The RTL_SUPPORT REPORT serial command: read-only, privacy-bounded support snapshot.
//
// A serial command family separated from process_command() in main.cpp (see serial_help.hpp for the convention).
// Everything the command reads from the device comes through Hooks, so the family builds and is tested on the host;
// it keeps no state of its own.
#include "support_report.hpp"

namespace orcsdr::serial_support {

// Every pointer must be set.
struct Hooks {
  void (*write_line)(const char* text) = nullptr;
  orcsdr::support_report::Snapshot (*snapshot)() = nullptr;
};

// Returns true when `command` (not null) was RTL_SUPPORT REPORT.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_support

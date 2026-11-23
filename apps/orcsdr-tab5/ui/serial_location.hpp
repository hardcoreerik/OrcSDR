#pragma once
// The RTL_LOCATION serial commands: STATUS, IP, LOOKUP <place> and CONFIRM.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device comes through Hooks, so the family builds and is tested on the host; it keeps
// no state of its own.
#include "location_estimate.hpp"

namespace orcsdr::serial_location {

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  location_estimate::State (*state)() = nullptr;    // a copy of the location estimate state
  bool (*request_ip)() = nullptr;                   // starts the IP lookup (with the current Wi-Fi state); true if queued
  bool (*request_lookup)(const char* query) = nullptr;   // starts a place lookup (with the current Wi-Fi state); true if queued
  void (*confirm)() = nullptr;                      // applies the estimate (the settings action location_ip_confirm)
};

// Returns true when `command` (not null) was an RTL_LOCATION command, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_location

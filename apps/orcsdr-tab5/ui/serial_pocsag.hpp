#pragma once
// The RTL_POCSAG serial commands: STATUS, SCAN, SCAN_STOP, SET_BAUD, SET_POLARITY and TUNE.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device goes through Hooks, so the family builds and is tested on the host; it
// keeps no state of its own.
//
// Every route except "RTL_POCSAG STATUS" has `authenticated` in its own condition: an unauthenticated line does not
// match, prints nothing and is left to the rest of the dispatcher.
#include <cstdint>

namespace orcsdr::serial_pocsag {

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  void (*print_status)() = nullptr;                 // the whole RTL_POCSAG STATUS reply
  bool (*on_pocsag_band)() = nullptr;               // the POCSAG band is the one on screen
  void (*request_scan)() = nullptr;                 // queues the discovery scan
  void (*request_scan_stop)() = nullptr;            // queues cancelling it
  void (*set_baud)(uint16_t baud_bps) = nullptr;    // 0 = AUTO; stores it and marks the configuration to be applied
  void (*set_polarity)(uint8_t mode) = nullptr;     // 0 = AUTO, 1 = NORMAL, 2 = INVERTED; same
  // Clamps to the POCSAG band, records it as the POCSAG frequency, queues the band switch; returns the clamped value.
  uint32_t (*tune)(uint32_t frequency_hz) = nullptr;
};

// Returns true when `command` (not null) was an RTL_POCSAG command that matched a route, whether it ran or replied
// with an error.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_pocsag

#pragma once
// The RTL_ADSB serial commands that set up the ADS-B dashboard: RTL_ADSB_RANGE, RTL_ADSB_RANGE <nm>,
// RTL_ADSB_LOCATION <lat> <lon>, RTL_ADSB_STOP and RTL_ADSB_START. (RTL_ADSB STATUS is a separate route.)
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device comes through Hooks, so the family builds and is tested on the host; it keeps
// no state of its own.
#include <cstdint>

namespace orcsdr::serial_adsb {

// The facts RTL_ADSB_RANGE prints.
struct RangeStatus {
  uint16_t range_nm = 0;             // the range in the receiver settings
  uint16_t dashboard_range_nm = 0;   // the range the dashboard is using
  bool active = false;               // the ADS-B dashboard is running
  bool pending = false;              // a settings write is waiting to be persisted
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  RangeStatus (*range_status)() = nullptr;
  void (*set_range)(uint16_t range_nm) = nullptr;   // applies a range already checked against the allowed ones
  // Stores the receiver position (1e-7 degrees) and returns false if it was refused or could not be saved.
  bool (*save_location)(int32_t latitude_e7, int32_t longitude_e7) = nullptr;
  void (*refresh_if_active)() = nullptr;            // re-enters the dashboard with the new position if it is running
  bool (*adsb_band_selected)() = nullptr;           // the ADS-B band is the current band
  void (*request_stop)() = nullptr;                 // asks the radio to stop
  void (*start)() = nullptr;                        // queues a local ADS-B listen on the default frequency
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_adsb

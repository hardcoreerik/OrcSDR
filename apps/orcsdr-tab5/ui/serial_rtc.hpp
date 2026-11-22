#pragma once
// The clock serial commands: ORC_RTC_STATUS, ORC_TZ_SET <minutes> and ORC_RTC_SET <utc seconds>.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device comes through Hooks, so the family builds and is tested on the host; it
// keeps no state of its own. The RTC chip stores UTC only; the offset is kept apart.
#include <cstdint>

namespace orcsdr::serial_rtc {

struct Clock {   // what ORC_RTC_STATUS reports
  bool valid = false;           // the wall clock has been set
  uint32_t utc = 0;
  bool hardware = false;        // the RTC chip is enabled (otherwise "unavailable")
  int32_t offset_min = 0;       // UTC offset in minutes
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  Clock (*clock)() = nullptr;
  bool (*valid_offset)(int32_t minutes) = nullptr;
  bool (*store_offset)(int32_t minutes) = nullptr;   // false: it could not be saved
  bool (*set_utc)(uint32_t utc) = nullptr;           // false: out of range or no clock to set
  void (*forget_clock_trust)() = nullptr;            // a hand-set time is not vouched for by anything
  bool (*mark_estimated)() = nullptr;                // saves the "time is an estimate" flag; false: not saved
  void (*settings_changed)() = nullptr;              // refresh the global settings and the screen
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_rtc

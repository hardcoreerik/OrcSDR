#pragma once
// FT8 WWV: the time-signal tick measurement.
//   FT8 WWV                                               status (no sign-in)
//   FT8 WWV <2.5|5|10|15|20> [seconds 20-240] [CAL|SYNC]  tune 1 kHz below the WWV carrier and fold the tick for
//                                                         `seconds` (default 90)
//   FT8 WWV STOP                                          end a measurement early
// CAL stores the fixed delay against a fresh NTP sync; SYNC corrects the clock from the tick (needs that
// calibration). The decoder must be running (FT8 RUN 1) and the band is put back afterwards. Starting and stopping
// need a session. Parsing and reply text moved unchanged from process_ft8_command() in main.cpp; tuning, the
// measurement and its state stay there behind Hooks.
#include <cstdint>

#include "serial_ft8_io.hpp"

namespace orcsdr::serial_ft8_wwv {

// The numbers are printed (action=<n>); main.cpp asserts they match its WwvAction.
enum class Action : unsigned char { none = 0, report = 1, calibrate = 2, sync = 3 };

struct State {
  unsigned phase = 0;      // 1 while a measurement runs
  unsigned seconds = 0;
  Action action = Action::none;   // the action waiting for the running measurement
  double mhz = 0.0;
};

// Every pointer must be set.
struct Hooks {
  orcsdr::serial_ft8::Io io;
  void (*state)(State* out) = nullptr;
  void (*stop)() = nullptr;
  bool (*frequency_supported)(float mhz) = nullptr;   // one of the WWV carriers (to the nearest 0.1 MHz)
  bool (*decoder_active)() = nullptr;
  // Remembers the band, tunes `dial_hz`, starts the fold and records the action; on failure puts the band back and
  // returns false.
  bool (*start)(float mhz, uint32_t dial_hz, uint32_t seconds, Action action) = nullptr;
};

// `verb` is upper case, `rest` the remainder of the line without leading spaces. True when the verb was WWV.
bool handle(const char* verb, const char* rest, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_ft8_wwv

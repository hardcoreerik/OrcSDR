#pragma once
// "FT8 <verb> ..." serial commands: the verb is read (first word, upper-cased, at most 15 characters) and the line is
// handed to the family that owns it:
//   serial_ft8_query    STATUS TIME BANDS DECODES HUNTSTATUS CLOCK           no sign-in
//   serial_ft8_data     DUMP (no sign-in); ADIF SAVE SHOT SNAP INJECT        signed in
//   serial_ft8_wwv      WWV (status no sign-in; start and STOP signed in)
//   serial_ft8_control  OPEN BAND MODE CLEAR RUN TAB HUNT DIAL CLOCKTEST NTP SNR MEM LOG CONFIG   signed in
// HELP (or no verb) prints the two help lines. An unknown verb gets auth_required when signed out and
// unknown_command when signed in. Moved unchanged from process_ft8_command() in main.cpp, which keeps the decoder,
// the tuner, the SD card and the screen behind the family hook tables.
#include "serial_ft8_control.hpp"
#include "serial_ft8_data.hpp"
#include "serial_ft8_io.hpp"
#include "serial_ft8_query.hpp"
#include "serial_ft8_wwv.hpp"

namespace orcsdr::serial_ft8 {

// Every pointer must be set.
struct Hooks {
  Io io;
  const orcsdr::serial_ft8_query::Hooks* query = nullptr;
  const orcsdr::serial_ft8_data::Hooks* data = nullptr;
  const orcsdr::serial_ft8_wwv::Hooks* wwv = nullptr;
  const orcsdr::serial_ft8_control::Hooks* control = nullptr;
};

// `args` is what follows "FT8" on the line (it starts at the space, or is ""). Always handles the line.
void handle(const char* args, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_ft8

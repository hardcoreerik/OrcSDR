#pragma once
// The RTL_TUNE and RTL_FREQ serial commands: RTL_TUNE <BAND> <HZ>, RTL_FREQ and RTL_FREQ <HZ>.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. This module adds
// no frequency limit of its own: the band names, the device check and every tuning limit stay behind the hooks, in
// the driver and the dongle's capabilities. It keeps no state of its own.
#include <cstdint>

namespace orcsdr::serial_tune {

// What the RTL_FREQ status line reports; the strings must stay valid until the hook is called again.
struct FreqStatus {
  const char* band = "";
  uint32_t frequency_hz = 0;
  const char* mode = "";
};

enum class TuneResult : uint8_t {
  unknown_band,   // the band name is not one the radio knows
  not_ready,      // no receiver is ready
  not_queued,     // the listen request was refused (the reply is silent)
  ok,             // queued; *band_label holds the band's name for the reply
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  FreqStatus (*freq_status)() = nullptr;            // RTL_FREQ
  // RTL_FREQ <HZ>: asks for a hot retune and says whether it was accepted.
  bool (*hot_retune)(uint32_t frequency_hz) = nullptr;
  const char* (*ui_band_name)() = nullptr;          // the band shown now, read after the retune
  // RTL_TUNE <BAND> <HZ>: looks the band up, checks the receiver and queues the listen, in that order.
  TuneResult (*tune)(const char* band_name, uint32_t frequency_hz, const char** band_label) = nullptr;
};

// Returns true when `command` (not null) was one of these commands. RTL_TUNE <...> and RTL_FREQ <...> are matched
// only for an authenticated caller; otherwise they are not ours and fall through.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_tune

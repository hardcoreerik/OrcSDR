#pragma once
// The receiver start/stop serial commands: RTL_STOP, RTL_CAPTURE [KZEL|NOAA|AM|LORA], RTL_LISTEN <FM|KZEL|NOAA|WX|AM|LORA>
// and RTL_VOLUME <n>.
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The routes,
// their order, the helper statements the route table records, the sign-in rules and every reply are the ones the
// dispatcher had. The receiver state stays in main.cpp; everything comes through Hooks, so this builds and is tested
// on the host and keeps no state.
#include <cstdint>

namespace orcsdr::serial_radio {

enum class Band : uint8_t { fm, wx, am, lora };

struct Queued {   // RTL_CAPTURE_QUEUED
  const char* band_name = "";
  uint32_t frequency_hz = 0;
  uint32_t sample_rate_sps = 0;
  uint32_t bytes = 0;
  unsigned volume = 0;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  bool stop_without_sign_in = false;                // the LoRa test and DSP lab builds accept RTL_STOP signed out
  bool (*stop)() = nullptr;                         // false: nothing to stop (flags cleared); true: stopping
  // Queues a capture of `band` at its default frequency; false when the receiver is missing or busy.
  bool (*queue_capture)(Band band, bool continuous, Queued* out) = nullptr;
  int volume_min = 0;
  int volume_max = 0;
  void (*set_volume)(uint8_t volume) = nullptr;     // requested, live and screen volume, speaker, UI refresh
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_radio

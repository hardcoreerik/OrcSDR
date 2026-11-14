#pragma once
// The capture and IQ-recording serial commands: RTL_CAPTURE_STATUS, RTL_IQ_START, RTL_IQ_DIAG_START <args>,
// RTL_IQ_DIAG_STATUS, RTL_IQ_STOP / RTL_IQ_SAVE and RTL_IQ_STATUS.
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The routes,
// their order, the sign-in rule (only RTL_IQ_DIAG_START needs it) and every reply are the ones the dispatcher had. The
// recording itself stays in main.cpp; everything comes through Hooks, so this builds and is tested on the host.
#include <cstdint>

namespace orcsdr::serial_iq {

struct CaptureStatus {   // RTL_CAPTURE_STATUS
  const char* state = "";
  bool finished = false;   // complete or failed: the counters below are printed
  uint64_t bytes = 0;
  unsigned min = 0;
  unsigned max = 0;
  double mean = 0;
  const char* sha256 = "";   // "" before the first capture: printed as none
  const char* error = "";
};

struct DiagStatus {   // RTL_IQ_DIAG_STATUS
  const char* transition = "";
  uint32_t sequence = 0;
  bool active = false;
  bool ready = false;
  uint32_t bytes = 0;
  uint32_t max_bytes = 0;
  uint32_t rate = 0;
  uint32_t frequency_hz = 0;
  uint32_t started_ms = 0;
};

struct IqStatus {   // RTL_IQ_STATUS
  const char* source = "";
  bool active = false;
  bool ready = false;
  bool energy_triggered = false;   // "energy" rather than "manual"
  uint32_t bytes = 0;
  uint32_t max_bytes = 0;
  uint32_t frequency_hz = 0;
  uint32_t sf = 0;
  uint32_t bandwidth_hz = 0;
  bool detector_on = false;
  uint32_t events = 0;
  uint32_t messages = 0;
  double noise_dbfs = 0;
  double trigger_dbfs = 0;
  const char* last_path = "";   // "" printed as none
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  CaptureStatus (*capture_status)() = nullptr;
  void (*iq_start)() = nullptr;                      // iq_rec_start, which prints its own reply
  void (*iq_diag_start)(const char* args) = nullptr; // diagnostic_iq_rec_start
  DiagStatus (*diag_status)() = nullptr;
  void (*iq_stop)() = nullptr;                       // forget the SD state so it is tried again, then iq_rec_stop_and_export
  IqStatus (*iq_status)() = nullptr;
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_iq

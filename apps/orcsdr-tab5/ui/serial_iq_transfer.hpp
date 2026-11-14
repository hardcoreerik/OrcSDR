#pragma once
// The serial transfer commands for captured IQ: the LoRa replay upload (RTL_LORA_REPLAY_BEGIN / _CHUNK / _ABORT) and
// the IQ download (RTL_IQ_RETRIEVE_BEGIN, RTL_IQ_GET_BEGIN / _CHUNK / _ABORT, RTL_IQ_RETRIEVE_END).
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The routes,
// their order, the sign-in rule (only the replay upload needs it) and every reply are the ones the dispatcher had. The
// transfers themselves stay in main.cpp; everything comes through Hooks, so this builds and is tested on the host.
#include <cstdint>

namespace orcsdr::serial_iq_transfer {

struct RetrieveInfo {   // RTL_IQ_RETRIEVE_READY
  uint32_t bytes = 0;
  uint32_t rate = 0;
  uint32_t frequency_hz = 0;
  uint32_t sf = 0;
  uint32_t bandwidth_hz = 0;
};

struct AbortInfo {   // RTL_IQ_GET_ABORTED
  bool ready = false;
  uint32_t bytes = 0;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  void (*replay_begin)(const char* args) = nullptr; // lora_replay_put_begin (prints its own replies)
  void (*replay_chunk)(const char* args) = nullptr;
  void (*replay_abort)() = nullptr;                 // lora_replay_put_abort("host_abort")
  // False when no finished capture can be read; otherwise marks it as being read and fills `info`.
  bool (*retrieve_begin)(RetrieveInfo* info) = nullptr;
  void (*get_begin)() = nullptr;                    // iq_get_begin
  void (*get_chunk)() = nullptr;                    // iq_get_chunk
  AbortInfo (*get_abort)() = nullptr;               // iq_get_reset, then the capture's state
  bool (*retrieve_end)() = nullptr;                 // releases the capture; true when LoRa reception resumes
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_iq_transfer

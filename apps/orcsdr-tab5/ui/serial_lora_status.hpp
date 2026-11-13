#pragma once
// The RTL_LORA_NATIVE_STATUS and RTL_LORA_PLAN_STATUS serial commands.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order and every reply are the ones the dispatcher had. Everything the commands read from the device
// comes through Hooks, so the family builds and is tested on the host; it keeps no state of its own.
#include <cstdint>

namespace orcsdr::serial_lora_status {

// The native LoRa decoder counters, read when the command runs.
struct NativeStatus {
  bool ready = false;
  bool busy = false;
  bool key_loaded = false;
  unsigned long crc_ok = 0;
  unsigned long encrypted = 0;
  unsigned long failures = 0;
  unsigned long last_decode_ms = 0;
  unsigned long preambles = 0;
  unsigned long header_failures = 0;
  unsigned long crc_failures = 0;
  double raw_cfo_hz = 0.0;
  double cfo_hz = 0.0;
};

// The selected LoRa channel plan. region_index is already one-based, as printed.
struct PlanStatus {
  unsigned region_index = 0;
  const char* region_name = "";
  unsigned slot = 0;
  unsigned slots = 0;
  unsigned default_slot = 0;
  uint32_t frequency_hz = 0;
  bool persisted = false;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;   // prints the text as given, in one write (it ends in '\n')
  NativeStatus (*native_status)() = nullptr;
  PlanStatus (*plan_status)() = nullptr;
};

// Returns true when `command` (not null) was one of these commands.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_lora_status

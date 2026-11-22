#pragma once
// The RTL_MEM serial commands: STATUS, BUFFERS, HOG and RECLAIM.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from the device comes through Hooks, so the family builds and is tested on the host; it keeps no
// state of its own.
#include <cstddef>
#include <cstdint>

#include "memory_ledger.hpp"

namespace orcsdr::serial_memory {

enum class Region : uint8_t {
  psram,      // external RAM
  internal,   // all internal RAM
  dma,        // internal RAM that DMA can use (Wi-Fi/C6 link, USB host, SD card, I2S): the scarce one
};

struct OrcDial {
  bool bridge = false;                  // the OrcDial transport is up
  uint32_t secure_stack_free_min = 0;   // smallest unused part of the orcdial_secure task stack so far
};

struct Ft8 {
  bool active = false;
  const char* last_failure = "none";    // "none", "memory" or "backend": why the decoder last failed to start
  uint32_t needed = 0;                  // the allocation that could not be satisfied
  uint32_t failures = 0;
};

// Every pointer must be set, except `orcdial`: leave it null in a build without the OrcDial bridge and the
// RTL_MEM_ORCDIAL line is not printed.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  uint32_t (*free_bytes)(Region region) = nullptr;
  uint32_t (*largest_block)(Region region) = nullptr;
  uint32_t (*minimum_free)(Region region) = nullptr;   // lowest free since boot
  OrcDial (*orcdial)() = nullptr;
  Ft8 (*ft8)() = nullptr;
  const memory_ledger::Buffer* buffers = nullptr;      // the large on-demand buffers reported by RTL_MEM BUFFERS
  size_t buffer_count = 0;
  memory_ledger::IsExternalRam is_external_ram = nullptr;
  size_t (*hog_fill)(size_t leave_free_bytes) = nullptr;   // bench aid: holds PSRAM, returns the bytes held
  void (*reclaim_and_retry)() = nullptr;   // releases every registered holder, prints what came back, retries FT8
};

// Returns true when `command` (not null) was an RTL_MEM command, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_memory

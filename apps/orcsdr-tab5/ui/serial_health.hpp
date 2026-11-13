#pragma once
// The RTL_HEALTH serial command: uptime, heap and stack figures on one line.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// route, its order and the reply are the ones the dispatcher had. Everything the command reads from the device comes
// through Hooks, so the family builds and is tested on the host; it keeps no state of its own.
#include <cstdint>

namespace orcsdr::serial_health {

// The device figures RTL_HEALTH prints, in the order it prints them.
struct Snapshot {
  uint32_t uptime_ms = 0;
  uint32_t free_heap = 0;
  uint32_t min_free_heap = 0;
  uint32_t dma_free = 0;            // internal RAM that DMA can use
  uint32_t dma_min = 0;
  uint32_t dma_largest = 0;
  uint32_t tasks = 0;
  uint32_t main_stack_hwm = 0;
  int reset_reason = 0;
  uint32_t internal_free = 0;       // internal RAM a byte access can use
  uint32_t internal_min = 0;
  uint32_t internal_largest = 0;
  uint32_t psram_free = 0;
  uint32_t psram_largest = 0;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  Snapshot (*snapshot)() = nullptr;
};

// Returns true when `command` (not null) was RTL_HEALTH.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_health

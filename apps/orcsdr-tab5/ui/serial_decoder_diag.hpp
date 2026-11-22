#pragma once
// The decoder diagnostic serial commands: RTL_ACARS STATUS / SELFTEST / ARCHIVETEST, RTL_AIS STATUS / BENCH /
// TEST ON / TEST OFF and RTL_DROPLOG / RTL_DROPLOG CLEAR.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. The decoders, the
// SD card and the drop log stay in main.cpp behind Hooks, so the family builds and is tested on the host; it keeps
// no state of its own.
#include <cstddef>
#include <cstdint>

namespace orcsdr::serial_decoder_diag {

// What RTL_ACARS SELFTEST reports (the fields of acars_selftest::Result that the reply prints).
struct AcarsSelfTest {
  bool allocated = false;
  unsigned cases = 0;
  unsigned passed = 0;
  unsigned missed = 0;
  unsigned duplicates = 0;
  uint32_t samples = 0;
  uint32_t total_us = 0;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)

  void (*acars_describe)(char* out, size_t capacity) = nullptr;        // the ACARS app status line
  void (*acars_format_status)(char* out, size_t capacity) = nullptr;   // the ACARS receiver status line
  AcarsSelfTest (*acars_selftest)() = nullptr;      // runs the known-signal check on a private decoder
  void (*acars_archive_test)(char* out, size_t capacity) = nullptr;    // runs the SD archive check, writes its reply line

  char* (*ais_line_alloc)(size_t capacity) = nullptr;   // a PSRAM line buffer for RTL_AIS STATUS, null when none
  void (*ais_line_free)(char* line) = nullptr;
  void (*ais_describe)(char* out, size_t capacity) = nullptr;
  void (*ais_format_status)(char* out, size_t capacity) = nullptr;
  unsigned (*ais_dsp_stack_hwm)() = nullptr;            // DSP task stack high-water mark, 0 when the task is not running
  void (*ais_bench)(char* out, size_t capacity) = nullptr;
  void (*ais_set_test_replay)(bool on) = nullptr;

  uint32_t (*droplog_count)() = nullptr;                // drops logged since the last clear (may exceed the capacity)
  uint32_t droplog_capacity = 0;                        // how many of them have a time and a fill recorded
  uint32_t (*droplog_now_ms)() = nullptr;
  uint32_t (*droplog_stream_started_ms)() = nullptr;
  uint32_t (*droplog_entry_ms)(uint32_t index) = nullptr;
  uint32_t (*droplog_entry_filled)(uint32_t index) = nullptr;
  void (*droplog_clear)() = nullptr;
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_decoder_diag

#pragma once
// The authenticated diagnostic commands RTL_SD_SELF_CHECK, RTL_SD_BENCH [MiB] and RTL_WIFI_C6_POWER OFF.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks, the benchmark size range and every reply are the ones the
// dispatcher had. The SD commands write to the SD card and the C6 command cuts power to the Wi-Fi coprocessor; all of
// that is done by the hooks, so the family builds and is tested on the host and keeps no state of its own.
namespace orcsdr::serial_bench {

// Every pointer must be set.
struct Hooks {
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  void (*sd_self_check)() = nullptr;                // runs the SD file-semantics check (it prints its own report)
  void (*sd_bench)(unsigned file_mib) = nullptr;    // runs the SD write benchmark with a file of this many MiB
  void (*c6_power_off)() = nullptr;                 // drops the C6 power rail
};

// Returns true when `command` (not null) was one of these commands.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_bench

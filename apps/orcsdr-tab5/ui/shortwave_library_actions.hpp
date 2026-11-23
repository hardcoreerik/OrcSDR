#pragma once
// The Shortwave dashboard's memory and logbook actions (save, edit, favourite, delete, export). Moved out of main.cpp's
// handle_shortwave_dashboard_action. No device headers: the SD card, the clock and the receiver readings come through Hooks.
#include "action_io.hpp"
#include "shortwave_dashboard.hpp"
#include "shortwave_library.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::shortwave_library_actions {

struct Clock {
  uint32_t utc = 0;
  bool wallclock_valid = false;
};

// Every pointer must be set. `state` points at the one library held in PSRAM by main.cpp.
struct Hooks {
  action_io::Io io;
  orcsdr::shortwave::LibraryState* state = nullptr;
  void (*ensure_loaded)() = nullptr;
  // False without touching `error` when no card is mounted, as the former `g_sd_fs != nullptr &&` did.
  bool (*save_memories)(const orcsdr::shortwave::MemoryTable& memories, char* error, size_t error_capacity) = nullptr;
  bool (*save_logs)(const orcsdr::shortwave::LogTable& logs, char* error, size_t error_capacity) = nullptr;
  bool (*export_logs)(const orcsdr::shortwave::LogTable& logs, char* error, size_t error_capacity) = nullptr;
  Clock (*now)() = nullptr;
  uint32_t (*ui_frequency_hz)() = nullptr;
  uint32_t (*filter_bandwidth_hz)() = nullptr;
  float (*signal_dbfs)() = nullptr;
  const char* (*mode_name_at)(uint32_t frequency_hz) = nullptr;   // ssb::mode_name(ssb::current(frequency_hz))
  // The receiver name for a logbook line. Builds the dashboard snapshot, as before, so the library is loaded and the
  // snapshot's own reads happen at the same point.
  void (*snapshot_device)(char* out, size_t capacity) = nullptr;
  const char* (*pending_memory_label)() = nullptr;
  const char* (*pending_memory_notes)() = nullptr;
  const char* (*pending_log_antenna)() = nullptr;
  const char* (*pending_log_notes)() = nullptr;
};

// Runs the action when it is one of the library kinds (save_memory, update_memory, favorite_memory, delete_memory,
// save_log, update_log, delete_log, export_log) and returns true; returns false for every other kind.
bool handle(const orcsdr::shortwave::Action& action, const Hooks& hooks);

}  // namespace orcsdr::shortwave_library_actions

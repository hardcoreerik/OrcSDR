#pragma once
// The RTL_RDS serial commands: CAPTURE_START, CAPTURE_STOP / CAPTURE_SAVE, CAPTURE_STATUS, REPLAY, REPLAY_SAMPLE
// (only in an ORCSDR_DSP_AB build) and STATUS.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order and every reply are the ones the dispatcher had; none of them checks authentication, as
// before. Everything the commands read from or do to the device goes through Hooks, so the family builds and is
// tested on the host; it keeps no state of its own.
#include <cstdint>

namespace orcsdr::serial_rds {

// What RTL_RDS_STATUS prints, read in one go.
struct Status {
  bool carrier_present = false;
  float carrier_signal_dbfs = 0;
  bool block_locked = false;
  uint32_t good_blocks = 0;          // of the best hypothesis
  uint32_t total_blocks = 0;
  int parity_streak[2] = {0, 0};
  bool pilot_pll_locked = false;
  const char* ps = "";               // programme service name
  const char* rt = "";               // radio text
  uint16_t pi = 0;
  float chip_rate = 0;               // chips per second the timing loop runs at
  float chip_rate_correction_ppm = 0;
  float i_lpf = 0;
  float q_lpf = 0;
  float chip_phase = 0;
  uint16_t group_info[4] = {0, 0, 0, 0};   // blocks A..D of the best hypothesis
  uint32_t driver_overruns = 0;
  uint32_t driver_drops = 0;
  uint32_t effective_sps = 0;
  uint32_t audio_chunks = 0;         // queued
  uint32_t audio_drops = 0;
};

// Every pointer must be set, except `replay_per_sample`: it is only called in an ORCSDR_DSP_AB build.
struct Hooks {
  void (*write)(const char* text) = nullptr;   // prints the text as given, in one write (it ends in '\n')
  void (*capture_start)() = nullptr;           // these three print their own replies
  void (*capture_stop_and_export)() = nullptr;
  void (*capture_status_print)() = nullptr;
  void (*replay)(const char* path) = nullptr;  // prints its own replies
  Status (*status)() = nullptr;
  void (*replay_per_sample)(const char* path) = nullptr;   // the same replay through the old per-sample feed
};

// Returns true when `command` (not null) was an RTL_RDS command.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_rds

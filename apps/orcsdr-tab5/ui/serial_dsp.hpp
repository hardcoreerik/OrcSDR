#pragma once
// The RTL_DSP serial commands: STATS in every build, LAB RATE and LAB in the Stage-2 frontend lab build
// (ORCSDR_DSP_LAB), AB CAPTURE, AB STORM and AB RUN in the Stage-1 A/B harness build (ORCSDR_DSP_AB).
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, their build-flag guards and every reply are the ones the dispatcher had. Everything the
// commands read from or do to the device comes through Hooks, so the family builds and is tested on the host; it
// keeps no state of its own.
#include <cstddef>
#include <cstdint>

// The firmware build defines both for every source file of the component (main/CMakeLists.txt); these are the same
// fallbacks main.cpp has, for a host build that names neither.
#ifndef ORCSDR_DSP_AB
#define ORCSDR_DSP_AB 0
#endif
#ifndef ORCSDR_DSP_LAB
#define ORCSDR_DSP_LAB 0
#endif

namespace orcsdr::serial_dsp {

// What RTL_DSP STATS prints after the windowed stage text.
struct Stats {
  const char* band = "";                // name of the band the receiver is on
  uint32_t rate = 0;                    // active sample rate, samples per second
  uint32_t iq_pipeline_drops = 0;       // the loss counters are cumulative since stream start, not windowed
  uint32_t driver_overruns = 0;
  uint32_t driver_drops = 0;
  uint32_t audio_chunks = 0;
  uint32_t audio_dropped_chunks = 0;
  uint32_t audio_ring_overruns = 0;
  uint32_t audio_submit_failures = 0;
};

// What one RTL_DSP AB STORM run measured.
struct Storm {
  uint32_t storm_ms = 0;             // how long issuing the requests took
  uint32_t applied = 0;              // resets the DSP task served since the storm began
  uint32_t pending = 0;              // reset request bits still waiting 50 ms after the last request
  uint32_t audio_chunks_500ms = 0;   // audio chunks queued in the 500 ms after that
};

// `write`, `write_line` and `stats` must always be set. The lab_ pointers are used only in a build with
// ORCSDR_DSP_LAB, the ab_ pointers only in one with ORCSDR_DSP_AB; leave them null otherwise. The members exist in
// every build so the struct is the same whatever the flags.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write (it ends in '\n')
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  // Writes the stage text for the window since the previous call into `line` (and starts a new window), then
  // returns the counters read after it.
  Stats (*stats)(char* line, size_t line_size) = nullptr;

  bool (*lab_radio_running)() = nullptr;                 // a stream is queued, starting or running
  void (*lab_set_rate_override)(uint32_t rate) = nullptr;   // 0 clears the override
  bool (*lab_rate_usable)(uint32_t rate) = nullptr;      // a receiver is attached and supports this rate
  bool (*lab_queue_fm_listen)() = nullptr;               // starts FM on the current (or default) FM frequency
  void (*lab_command)(const char* args) = nullptr;       // the frontend lab's own command line; prints its replies

  bool (*ab_arm)(size_t blocks) = nullptr;               // arms a capture of this many blocks
  Storm (*ab_storm)(long requests) = nullptr;            // runs the reset storm (blocks for its whole length)
  void (*ab_request_run)() = nullptr;
};

// Returns true when `command` (not null) was an RTL_DSP command of this build. `authenticated` is unused: none of
// these routes checked it in the dispatcher.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_dsp

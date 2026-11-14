#pragma once
// FM multiplex (MPX) capture for repeatable RDS debugging: RTL_RDS_CAPTURE_START records 8 s of the demodulated FM
// composite at 240 kS/s into a 3.84 MB PSRAM buffer; RTL_RDS_CAPTURE_STOP saves it to the SD card as
// /orcsdr/rds_debug/NNN_<frequency>_mpx.s16 plus a .json note, for RTL_RDS_REPLAY or a PC decoder such as redsea.
// Live RDS decoding does not use any of this.
//
// Moved out of main.cpp (2026-11-13). The save now runs on the background SD writer (sd_writer.hpp): STOP answers at
// once and RTL_RDS_CAPTURE_SAVED (or the error and RTL_RDS_CAPTURE_PSRAM_HOLD) follows from service() when the card
// write has finished. The buffer is a memory_reclaim holder, given back once its capture is on the card.
//
// Threads: start/stop/status/service run on the main loop; tap_begin/tap_end run on the DSP task once per block;
// the reclaim holder may be called from the main loop or the radio driver task. Everything device-specific comes in
// through Hooks, so this builds and is tested on a host.
#include <cstddef>
#include <cstdint>

#include "sd_writer.hpp"

namespace orcsdr::rds_capture {

constexpr uint32_t kRateHz = 240000;
constexpr size_t kSeconds = 8;
constexpr size_t kSamples = kRateHz * kSeconds;
constexpr size_t kBufferBytes = kSamples * sizeof(int16_t);

struct Hooks {
  void (*write)(const char* text) = nullptr;       // one write (Serial.printf)
  void (*write_line)(const char* text) = nullptr;  // text, then a newline as a second write (Serial.println)
  bool (*fm_selected)() = nullptr;                 // the receiver's band is FM
  uint32_t (*frequency_hz)() = nullptr;
  uint32_t (*now_ms)() = nullptr;
  int16_t* (*alloc)(size_t bytes) = nullptr;       // PSRAM
  void (*release)(int16_t* buffer) = nullptr;
  bool (*sd_ready)() = nullptr;                    // mounts the card if needed (main loop only)
  const char* (*sd_state)() = nullptr;             // "ready", "missing" or "untried"
  bool (*exists)(const char* path) = nullptr;
  bool (*mkdir)(const char* path) = nullptr;
  bool (*remove)(const char* path) = nullptr;
  void (*wait)() = nullptr;                        // sleep about a millisecond
  sd_writer::Writer& (*writer)() = nullptr;
  double scale_radians_per_lsb = 0.0;              // written into the .json note
};

// Serial actions. Each prints its own reply through the hooks.
bool start(const Hooks& hooks);
bool stop_and_export(const Hooks& hooks);
void status_print(const Hooks& hooks);

// Main loop, every pass: finishes a save that the background writer has completed.
void service(const Hooks& hooks);

// DSP task, once per block. tap_begin returns false when no capture is running; otherwise write up to
// `capacity - write` samples to data[write++] and hand the tap back with tap_end, which prints
// RTL_RDS_CAPTURE_FULL when the buffer has just filled.
struct Tap {
  int16_t* data = nullptr;
  size_t write = 0;
  size_t capacity = 0;
};
bool tap_begin(Tap* tap);
void tap_end(const Tap& tap, const Hooks& hooks);

// RTL_MEM BUFFERS.
const void* buffer();

// Memory reclaim holder ("RDS capture"); registered by start().
size_t held_bytes();
size_t release_buffer();

// True while a save is queued or being written.
bool saving();

void reset_for_tests();

}  // namespace orcsdr::rds_capture

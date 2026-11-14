#pragma once
// Post-demodulation audio recording (the REC button, RTL_REC_START/STOP/STATUS/SAVE): up to 12 s of 48 kHz mono PCM in
// a 1.15 MB buffer, saved to the SD card as /orcsdr/rec_NNN_<band>_<frequency>.wav.
//
// Moved out of main.cpp (2026-11-13). The card write runs on the background SD writer (sd_writer.hpp): STOP answers
// at once and RTL_REC_WAV ok (or the error and RTL_REC_PSRAM_HOLD) follows from service() on the main loop. A full
// buffer used to be saved on the radio driver task; the DSP now only raises a flag and service() starts the save, so
// no radio or audio task waits on the card. The buffer is a memory_reclaim holder, given back once its recording is in
// a file.
//
// Threads: start/stop/status/service run on the main loop; append runs on the DSP task; readers (active(), samples(),
// ...) may run anywhere; the reclaim holder may be called from the main loop or the radio driver task.
#include <cstddef>
#include <cstdint>

#include "sd_writer.hpp"

namespace orcsdr::audio_rec {

constexpr uint32_t kRateHz = 48000;        // matches the speaker path
constexpr size_t kMaxSeconds = 12;
constexpr size_t kMaxSamples = kRateHz * kMaxSeconds;
constexpr size_t kWavHeaderBytes = 44;

struct Hooks {
  void (*write)(const char* text) = nullptr;       // one write (Serial.printf)
  void (*write_line)(const char* text) = nullptr;  // text, then a newline as a second write (Serial.println)
  uint8_t (*band)() = nullptr;                     // the receiver's band, as the firmware's band number
  const char* (*band_name)(uint8_t band) = nullptr;
  bool (*band_is_lora)() = nullptr;                // LoRa records IQ, not audio
  uint32_t (*frequency_hz)() = nullptr;
  int16_t* (*alloc_psram)(size_t bytes) = nullptr;
  int16_t* (*alloc_internal)(size_t bytes) = nullptr;  // used only when PSRAM has no room (unchanged policy)
  void (*release)(int16_t* buffer) = nullptr;
  bool (*psram_has_room)() = nullptr;              // the psram=yes|no in RTL_REC_BUF
  bool (*sd_ready)() = nullptr;                    // mounts the card if needed
  const char* (*sd_state)() = nullptr;             // "ready", "missing" or "untried"
  bool (*exists)(const char* path) = nullptr;
  bool (*mkdir)(const char* path) = nullptr;
  bool (*remove)(const char* path) = nullptr;
  void (*wait)() = nullptr;                        // sleep about a millisecond
  void (*ui_refresh)() = nullptr;                  // ask the screen to redraw the radio controls
  sd_writer::Writer& (*writer)() = nullptr;
};

// Serial and button actions. Each prints its own reply.
bool start(const Hooks& hooks);
bool stop_and_export(const Hooks& hooks);
void status_print(const Hooks& hooks);

// Main loop, every pass: saves a recording whose buffer has filled, and finishes a save the writer has completed.
void service(const Hooks& hooks);

// DSP task: copies post-demodulation samples while a recording runs. Prints RTL_REC_FULL once when the buffer fills.
void append(const int16_t* samples, size_t count, const Hooks& hooks);

// Readers for the screen and the radio.
bool active();
bool full();
size_t samples();
uint8_t band();
uint32_t frequency_hz();
const char* last_path();  // "" before the first save
bool saving();

// RTL_MEM BUFFERS.
const void* buffer();
size_t buffer_bytes();

// Memory reclaim holder ("Audio recording"); registered by start().
size_t held_bytes();
size_t release_buffer();

// The 44-byte RIFF/WAVE header for `samples` mono 16-bit samples at kRateHz.
void wav_header(uint8_t out[kWavHeaderBytes], size_t samples);

void reset_for_tests();

}  // namespace orcsdr::audio_rec

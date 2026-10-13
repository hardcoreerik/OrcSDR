#pragma once

#include <cstddef>
#include <cstdint>

// Clipping guard for the LoRa band.
//
// The tuner's own AGC can leave the ADC clipping when strong signals sit near the
// channel (for example a cellular carrier next to 869 MHz), which desensitises the
// receiver. By default the tuner stays on AUTO. Only when the measured clipping
// stays above a limit does the guard take over: it steps the dongle's own gain
// ladder down until the clipping is gone, holds there, and slowly probes back up.
// It never uses a fixed gain, and any manual gain command, switch of band or
// RTL_LORA_GAIN GUARD OFF releases it and restores the tuner AGC.

namespace orcsdr::lora_gain_guard {

struct Config {
  bool enabled = true;
  float engage_clip_percent = 1.0f;    // start acting above this ...
  float release_clip_percent = 0.2f;   // ... and keep reducing until below this
  uint32_t arm_delay_ms = 8000;        // ignore the start-up gain transient after LoRa begins
  uint32_t engage_ms = 1500;           // clipping must persist this long before acting
  // The clipping figure is smoothed over about a second, so after a step it still
  // shows the old value for a while. Wait for it to settle before judging the step,
  // otherwise stale readings drive the gain far lower than the clipping requires.
  uint32_t step_interval_ms = 2500;    // time between ladder steps while reducing
  uint32_t probe_up_ms = 60000;        // clip-free time before trying one step up
  uint32_t ceiling_memory_ms = 600000; // a step that caused clipping is avoided this long
  int start_gain_tenth_db = 280;       // ladder step closest to this is the first manual gain
};

enum class Phase : uint8_t { off, watching, reducing, holding };

struct Status {
  Phase phase = Phase::off;
  bool enabled = true;
  bool user_override = false;
  int gain_tenth_db = -1;  // current guard-set gain, -1 while the tuner AGC runs
  uint32_t reductions = 0;
  uint32_t probes = 0;
  float clipping_percent = 0;
};

// Supplied by the application.
struct Hooks {
  bool (*lora_receiving)() = nullptr;           // LoRa band is on and the radio is streaming
  float (*clipping_percent)() = nullptr;        // smoothed percentage of clipped samples
  size_t (*gains)(int* out, size_t capacity) = nullptr;  // ascending ladder in tenths of dB
  bool (*set_manual_gain)(int gain_tenth_db) = nullptr;
  bool (*set_tuner_auto)() = nullptr;
  void (*write_line)(const char* text) = nullptr;
};

void configure(const Config& config);
Config config();
Status status();

// Call regularly from the main loop (cheap when idle).
void tick(uint32_t now_ms, const Hooks& hooks);

// A user or another feature changed the gain; stop managing it until LoRa is re-entered.
void note_user_gain_change();

// LoRa is being left or re-entered: restore the tuner AGC if the guard had taken over.
void release(const Hooks& hooks);

// Handles RTL_LORA_GAIN STATUS | GUARD ON|OFF | LIMIT <percent>. Returns true if handled.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::lora_gain_guard

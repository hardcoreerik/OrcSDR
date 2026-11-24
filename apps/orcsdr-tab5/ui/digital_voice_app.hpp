#pragma once
// Digital Voice application glue. Owns the dashboard lifecycle, the scanner (dv_scanner) and the receive attachment
// (dv_radio), and turns validated decoder events into the dashboard's immutable Snapshot. It never opens the radio
// driver: the receiver owner performs every tune through Hooks, and the dashboard only ever sees snapshots.
// main.cpp forwards: open(), close(), tick(), draw(), touch(), active(), handle_command() and, from the DSP task,
// dv_radio::offer_iq().
#include <cstddef>
#include <cstdint>

namespace orcsdr::digital_voice_app {

struct Hooks {
  void (*header_controls)() = nullptr;   // shared battery / home / volume / visualizer / settings controls
  bool (*overlay_open)() = nullptr;      // shared volume tray is open over the content: do not paint under it
  bool (*receiver_ready)() = nullptr;    // an RTL-SDR is present
  void (*go_home)() = nullptr;           // memory failure: never leave an undrawn screen
  uint32_t (*now_ms)() = nullptr;
  // Voice audio (the existing speaker sink and the user's sound switch; see dv_voice_service.hpp).
  void (*audio_out)(const int16_t* mono48k, size_t samples) = nullptr;
  bool (*audio_allowed)() = nullptr;
  bool (*audio_path_ready)() = nullptr;
  void (*audio_exclusive)(bool on) = nullptr;
  // Speech recording (SETUP toggle): a file write through the shared background SD writer (header + data, buffer valid until
  // file_save_result() reports 1 = done or 2 = failed) and a persistent boolean setting.
  bool (*file_save)(const char* path, const void* header, size_t header_len, const void* data, size_t length) = nullptr;
  int (*file_save_result)() = nullptr;
  bool (*flag_load)(const char* key) = nullptr;
  void (*flag_store)(const char* key, bool value) = nullptr;
  uint32_t (*u32_load)(const char* key, uint32_t fallback) = nullptr;   // band picker selection (persistent)
  void (*u32_store)(const char* key, uint32_t value) = nullptr;
  uint32_t (*utc_epoch)() = nullptr;     // wall-clock seconds, 0 when the clock is not valid (times then stay unknown)
  // Receiver-owner access (the same tune / release rules ACARS and AIS use; no second owner).
  bool (*tune)(uint32_t center_hz, uint32_t sample_rate) = nullptr;
  void (*release)() = nullptr;
  uint32_t (*micros)() = nullptr;
  uint64_t (*lost_samples)() = nullptr;
  // Scan plan persistence: a comma-separated list of Hz. load returns false when nothing is stored.
  bool (*load_plan)(char* out, size_t capacity) = nullptr;
  void (*save_plan)(const char* csv) = nullptr;
  // Replay of a recorded 240 kS/s CU8 file from the SD card (RTL_DV REPLAY). Read returns 0 at the end of the file.
  bool (*replay_open)(const char* path) = nullptr;
  size_t (*replay_read)(uint8_t* buffer, size_t capacity) = nullptr;
  void (*replay_close)() = nullptr;
  // Persistent discovery database (text, <= 24 KB). load returns the byte count (0 = no file). save hands the buffer to the
  // background SD writer and returns false while that is busy; the buffer stays valid and untouched until save_result()
  // reports 1 (done) or 2 (failed); 0 = still writing.
  size_t (*db_load)(char* out, size_t capacity) = nullptr;
  bool (*db_save)(const char* data, size_t length) = nullptr;
  int (*db_save_result)() = nullptr;
};

void install(const Hooks& hooks);
void open();                       // screen opened: allocate PSRAM state, first full draw. Does NOT start the receiver.
void close();                      // left for any reason; stops scanning, releases the receiver, frees everything.
void tick();                       // main loop: scanner, events, snapshot, repaint on change
void draw(bool static_panel);      // ScreenController refresh (true = full redraw after Settings/Visualizer)
void touch(int32_t x, int32_t y);  // taps below the shared header
bool active();
void describe(char* out, size_t size);   // one "RTL_DV_UI ..." serial line
// RTL_DV STATUS | PLAN <csv> | SCAN START|STOP|PAUSE|RESUME|HOLD|SKIP | MASK <n> | REPLAY <sd path> <channel Hz> | REPLAY STOP.  True when `command` was one of these.
bool handle_command(const char* command, bool authenticated, void (*write_line)(const char*));

}  // namespace orcsdr::digital_voice_app

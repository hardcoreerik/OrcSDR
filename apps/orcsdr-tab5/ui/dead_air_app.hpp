#pragma once
// Dead Air application glue: owns the dashboard lifecycle, the PSRAM-backed event model/queue, the
// SD journal worker task and the receiver-to-Snapshot adapter. main.cpp supplies only what it alone
// can reach (radio state, tuning, mute, battery, clock) through Hooks and forwards: open(), close(),
// tick(), draw(), touch() and feed_spectrum().
//
// Nothing here renders from a USB/IQ/DSP task and nothing here touches the SD card on the UI task:
// the journal is written by a dedicated low-priority worker that drains a bounded SPSC queue.
#include <cstddef>
#include <cstdint>

namespace orcsdr::dead_air_app {

struct Hooks {
  bool (*receiver_ready)() = nullptr;      // dongle open and stream running
  uint32_t (*frequency_hz)() = nullptr;    // tuned frequency
  const char* (*demod)() = nullptr;        // demodulator actually in use (e.g. "NFM")
  void (*set_demod)(uint8_t index) = nullptr;  // index into kModeNames; the receiver's own demodulator
  void (*release_demod)() = nullptr;       // give the demodulator choice back (screen left)
  uint32_t (*sample_rate)() = nullptr;     // IQ sample rate, S/s
  uint32_t (*config_signature)() = nullptr;  // changes whenever frequency, rate, gain or mode changes
  uint32_t (*rf_sample_counter)() = nullptr; // monotonically increasing; advances when new IQ was measured
  bool (*rf_level_dbfs)(float* out) = nullptr;  // latest measured IQ power in dBFS; false when none
  void (*tune)(uint32_t hz) = nullptr;     // retune (the receiver settles on its own schedule)
  bool (*hot_retune)(uint32_t hz) = nullptr;  // cheap retune within the current band; false if it was not accepted
  uint32_t (*step_frequency)(uint32_t hz, int direction) = nullptr;
  bool (*sound_enabled)() = nullptr;
  bool (*overlay_open)() = nullptr;        // the shared volume tray is open over the content
  int (*battery_percent)() = nullptr;
  uint32_t (*utc_epoch)() = nullptr;       // 0 when the wall clock is not verified
  bool (*sd_ready)() = nullptr;            // mounts on demand; true when the card is usable
  void (*toggle_mute)() = nullptr;
  bool (*replay_begin)(const int16_t* pcm, size_t samples) = nullptr;  // play 48 kHz mono PCM on its own speaker channel
  bool (*replay_playing)() = nullptr;
  void (*replay_stop)() = nullptr;         // stop a replay and give the speaker back to the receiver
  void (*go_home)() = nullptr;
  void (*open_settings)() = nullptr;
};

void install(const Hooks& hooks);
bool ready_to_open();              // waits for the last session's worker; false = cannot open now (do not retune the radio)
void open();                    // screen opened: allocate state, start worker, first draw
void close();                      // left for any reason; stops the worker and closes the file
void tick();                       // main loop: build snapshot, run detector, repaint on change
void draw(bool static_panel);      // refresh requested by the ScreenController
void touch(int32_t x, int32_t y);
bool active();
bool wants_spectrum();
// DSP thread: offer the receiver's post-demodulation mono 48 kHz PCM. Lock-free, allocation-free, constant time; a no-op
// unless Dead Air is open. It measures level only.
void offer_audio(const int16_t* pcm, size_t count);
// True while the Audio tab is visible: the receiver then demodulates even if the speaker is muted.
bool wants_audio();             // true while the Spectrum tab is the visible, owned page
// UI-task only. `levels` is the receiver's smoothed power spectrum (10*log10 of raw FFT power over
// `bins` bins, windowed CU8 samples). It is converted to relative dBFS here.
void feed_spectrum(const float* levels, size_t bins, uint32_t sample_rate_sps);
// Bench test (authenticated serial only): log `count` manual marks in one burst, faster than the SD worker drains the
// queue, to exercise overflow accounting. Returns the number logged (0 when Dead Air is not open).
uint32_t flood_manual_marks(uint32_t count);
void describe(char* out, size_t size);  // one diagnostic line for the serial console
void describe_env(char* out, size_t size);  // ENV III state and counters (RTL_DEAD_AIR ENV); only meaningful while the screen is open
void describe_veil(char* out, size_t size);  // VEIL-26 state and counters (RTL_DEAD_AIR VEIL)
// Authenticated serial only. `args` is "ON", "OFF" or "INTENT <text>"; fills `reply` with one line. False on error.
bool veil_command(const char* args, char* reply, size_t size);

}  // namespace orcsdr::dead_air_app

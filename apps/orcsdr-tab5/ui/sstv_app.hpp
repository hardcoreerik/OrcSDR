#pragma once
// SSTV application glue: owns the dashboard lifecycle, settings, user actions and the hand-off
// of finished images to the background SD archive. main.cpp supplies only what it alone can do
// (RTL-SDR tuning, SD mount state, the preferences store, navigating home) through Hooks and
// forwards five calls: open(), close(), tick(), draw() and touch().
#include <cstddef>
#include <cstdint>
#include "sstv_settings.hpp"

namespace orcsdr::sstv_app {

// What the receiver reports about its tuner gain, for the TUNE tab. steps are in tenths of a dB, ascending.
struct GainInfo {
  bool available = false;
  bool is_auto = true;
  int tenth_db = 0;
  int steps[32]{};
  size_t step_count = 0;
};

struct Hooks {
  bool (*receiver_ready)() = nullptr;                // an RTL-SDR is attached and usable
  bool (*streaming)() = nullptr;                     // IQ is flowing
  bool (*sd_ready)(bool mount_if_needed) = nullptr;  // SD card mounted (optionally try to mount)
  void (*tune)(uint32_t frequency_hz) = nullptr;     // point the RTL-SDR at an SSTV frequency
  void (*release_tuning)() = nullptr;                // undo tune() when the dashboard closes
  bool (*gain_info)(GainInfo* info) = nullptr;       // tuner gain state; false when there is nothing to adjust
  void (*gain_auto)() = nullptr;                     // hand gain to the tuner AGC
  void (*gain_manual)(int tenth_db) = nullptr;       // fixed tuner gain
  void (*set_demod)(uint8_t demod) = nullptr;        // sstv_tune::Demod the receiver should demodulate with
  uint32_t (*sample_rate)() = nullptr;               // IQ sample rate of the running stream (0 = not running)
  void (*set_if_bandwidth)(uint32_t hz) = nullptr;   // IF filter width ahead of the FM discriminator (0 = the radio default)
  uint32_t (*if_bandwidth)() = nullptr;              // the IF width the receiver is applying now (override or default)
  void (*header_controls)() = nullptr;               // draws the shared header controls (battery, home, ...)
  uint32_t (*utc_epoch)() = nullptr;                 // verified UTC seconds, 0 while unverified
  orcsdr::sstv_settings::Store store;                // persistent preferences
};

void install(const Hooks& hooks);   // call before open(); safe to repeat
void open();                        // dashboard opened: settings, receiver, archive, first draw, tune
void close();                       // dashboard left for any reason; no-op when it is not open
void tick();                        // main loop: decoder service and archive hand-off
void draw(bool static_panel);       // screen refresh requested by the screen controller
void touch(int32_t x, int32_t y);   // touch routing
// Queue an image for the SD archive: -1 = image in progress, otherwise a gallery index. False
// when nothing could be queued (no card, nothing to save, queue busy); the UI is told why.
bool save_image(int32_t which);

// Direct tuning, shared by the touch screen, the OrcDial and the serial console. All are no-ops (false) while the
// dashboard is closed.
bool active();
uint32_t frequency_hz();
bool tune_to(uint32_t frequency_hz);          // false when outside the receiver's range
bool nudge(int direction, bool fine = false); // one VFO step up (+1) or down (-1); fine uses the smallest step
uint32_t step_hz();
bool set_step(uint32_t step_hz);              // snaps to the nearest step on the list
uint32_t next_step_hz(bool larger);           // the neighbouring step on the list (stays at the ends)
void cycle_demod();                           // AUTO -> NFM -> USB -> LSB -> AUTO
uint32_t if_bandwidth_hz();                  // the IF width in use
void cycle_if_bandwidth();                    // next width on the list, wrapping
// Serial control of the TUNE tab's settings ("MODE AUTO|NFM|USB|LSB", "IFBW <hz>", "STEP <hz>", "RAILS ON|OFF"). Writes a
// short reply (or the reason) and returns false when it was refused.
bool tune_command(const char* args, char* reply, size_t size);
void tune_summary(char* out, size_t size);    // "frequency_hz=... demod=... step_hz=... if_hz=... rails=..."
const char* demod_label();                    // e.g. "NFM", "USB (AUTO)"
// RF spectrum for the TUNE tab, called from the UI task each time a spectrum frame exists.
bool wants_spectrum();
void feed_spectrum(const float* levels_db, size_t bins, uint32_t rate_sps);

}  // namespace orcsdr::sstv_app

#pragma once
// Native M5GFX SSTV dashboard (RECEIVE, IMAGES, SIGNAL, TUNE, SETUP).
// Drawing and touch hit-testing only: the dashboard never changes the receiver or touches the
// SD card. User choices come back as Actions that sstv_app applies.
#include <cstddef>
#include <cstdint>
#include "sstv_receiver.hpp"
#include "sstv_settings.hpp"

namespace orcsdr::sstv {
enum class ActionKind : uint8_t {
  none,
  tune_profile,     // value = profile index
  set_detection,    // value = sstv_settings::Detection (automatic or vis_only)
  select_mode,      // value = Mode ordinal; chooses manual detection for that mode
  save_image,       // value = gallery index, or -1 for the image in progress
  clear_gallery,
  toggle_autosave,
  abort_rx,
  tune_hz,          // value = frequency in Hz (tap on the spectrum, keypad entry)
  nudge,            // value = +1 / -1: one VFO step up or down
  set_step,         // value = index into sstv_tune::step_hz
  cycle_demod,      // AUTO -> NFM -> USB -> LSB -> AUTO
  gain_toggle,      // automatic <-> manual receiver gain
  gain_step,        // value = +1 / -1: one tuner gain step (switches to manual)
  toggle_rails,     // guide rails on the audio comb
  apply_correction, // value = Hz to add to the dial, from the measured sync tone
  cycle_if          // next IF filter width
};
struct Action { ActionKind kind = ActionKind::none; int32_t value = 0; };
// Tuning state the application owns and the TUNE tab shows.
struct TuneView {
  uint8_t step_index = 3;       // index into sstv_tune::step_hz
  uint8_t demod = 0;            // sstv_tune::Demod in effect
  bool demod_auto = true;       // the demodulator follows the frequency
  bool rails = true;            // guide rails on the audio comb
  bool gain_available = false;
  bool gain_auto = true;
  int16_t gain_tenth_db = 0;
  uint32_t rate_sps = 240000;   // IQ rate behind the RF spectrum
  uint32_t if_hz = 25000;       // IF filter width the receiver applies ahead of the FM discriminator
  bool if_auto = true;          // the radio's own width, not an SSTV override
};
inline bool operator==(const TuneView& a, const TuneView& b) {
  return a.step_index == b.step_index && a.demod == b.demod && a.demod_auto == b.demod_auto && a.rails == b.rails &&
         a.gain_available == b.gain_available && a.gain_auto == b.gain_auto && a.gain_tenth_db == b.gain_tenth_db &&
         a.rate_sps == b.rate_sps && a.if_hz == b.if_hz && a.if_auto == b.if_auto;
}
inline bool operator!=(const TuneView& a, const TuneView& b) { return !(a == b); }
struct UiSnapshot {
  bool receiver_ready = false;
  bool streaming = false;
  bool sd_ready = false;
  uint32_t frequency_hz = 0;
  float rf_dbfs = -90;
  sstv_settings::Settings settings{};
  TuneView tune{};
};
void ui_enter(const UiSnapshot& snapshot);
void ui_leave();
void ui_update(const UiSnapshot& snapshot);
bool ui_active();
Action ui_touch(int32_t x, int32_t y);
Page ui_page();
uint8_t ui_profile();
void ui_set_profile(uint8_t index);
void ui_set_settings(const sstv_settings::Settings& settings);   // redraws; nothing is restarted
void ui_set_tune(const TuneView& tune);   // redraws when the TUNE tab is showing
void ui_notice(const char* message);
// The TUNE tab is on screen, so the application should compute the RF spectrum and pass it to ui_feed_spectrum() (the
// rows of dB levels with the tuned frequency at the middle bin, `rate_sps` wide in total). Called from the UI task.
bool ui_wants_spectrum();
void ui_feed_spectrum(const float* levels_db, size_t bins, uint32_t rate_sps);
// The shared battery / home / settings / mute controls, drawn by the application into the header
// (the same hook every other OrcSDR dashboard uses). Home and Settings touches are handled globally.
void set_header_hook(void (*draw_controls)());
// Time spent drawing, measured around every screen update (display latency, bench diagnostics).
struct UiStats { uint32_t draw_us_last = 0, draw_us_max = 0, draws = 0; };
UiStats ui_stats();
} // namespace orcsdr::sstv

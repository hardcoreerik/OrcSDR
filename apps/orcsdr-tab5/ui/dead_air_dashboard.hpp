#pragma once
#include "dead_air_model.hpp"
#include <cstdint>

namespace orcsdr::dead_air {

enum class ActionKind : uint8_t {
  none, close, toggle_mute, open_settings, start_scan, stop_scan,
  start_recording, stop_recording, save_clip, export_session, play_clip,
  tune_up, tune_down, set_demod, sweep_preset, sweep_dwell, toggle_clips, verify_clip, toggle_mic, select_clip, stop_replay,
  tune_to,          // value = frequency in Hz (a tap on the spectrum or on the band map)
  scrub_open,       // value = event sequence whose trace the scrub view shows
  scrub_close,
  play_event_clip,  // value = event sequence: verify and play the clip that covers it
  control_start,
  control_stop,
  toggle_chime,
  veil_open,        // value = event sequence to inspect (0 = the live stream)
  veil_close,
  veil_toggle,      // analysis on / off
  veil_alphabet,    // next display alphabet (never restarts analysis)
  veil_intention    // next preset intention (restarts the stream)
};

// Full-body views opened from a button; the tab bar stays and a tab tap closes them.
enum class Overlay : uint8_t { none, scrub, control, session, veil };
// Real receiver demodulators only. Sweep patterns and generated audio are not demodulators.
constexpr const char* kModeNames[] = {"NFM", "WFM", "AM", "USB", "LSB"};
constexpr int kModeCount = 5;

struct Action { ActionKind kind = ActionKind::none; int32_t value = 0; };

// All of these entry points run on the OrcSDR UI task. They do not own
// receiver, USB, DSP, I2C or SD hardware. Live observations are supplied
// through Snapshot by an application-level adapter.
void attach_events(EventModel* model);
void enter(const Snapshot& snapshot);
void update(const Snapshot& snapshot);
void leave();
bool active();
struct PaintStats {
  uint32_t compose_us = 0, diff_us = 0, push_us = 0, push_us_max = 0;  // last body refresh
  uint32_t pixels = 0, rects = 0, whole_pushes = 0, partial_pushes = 0;
  uint32_t frame_us = 0, frame_us_max = 0, frames = 0;  // direct-drawn spectrum frames
};
PaintStats paint_stats();

// Live spectrum, drawn straight onto the panel (UI task only). `dbfs` holds `count` measured points in relative dBFS.
// `epoch` changes whenever a retune or setting change makes earlier rows incomparable; the waterfall then gets a marker
// row. Returns false (drawing nothing) unless the Spectrum tab is the visible, owned screen.
constexpr size_t kTracePoints = 216;
bool draw_spectrum_frame(const float* dbfs, size_t count, uint32_t epoch, uint32_t now_ms);

// Live audio level view for the Audio tab, drawn straight onto the panel like the spectrum. `envelope` holds `count`
// peak levels normalised 0..1 over -70..0 dBFS (oldest first). All levels are measured, never synthesised; with
// `flowing` false nothing is drawn but a plain "not demodulating" notice. Returns false off the Audio tab.
struct AudioView {
  const float* envelope = nullptr;  // the whole recent history, used to seed the view
  size_t count = 0;
  const float* fresh = nullptr;     // bars measured since the last call (oldest first); the view scrolls by these
  size_t fresh_count = 0;
  bool flowing = false;
  float rms_dbfs = -100.0f, peak_dbfs = -100.0f;
  bool baseline_ready = false;
  float baseline_dbfs = 0.0f;
};
bool draw_audio_frame(const AudioView& view, uint32_t now_ms);
Page page();
Overlay overlay();
// The orb: a logged event bursts it (kind from the event, rise in dB above its baseline), and draw_fx() animates it on
// the Overview and in the header of every tab. draw_fx() paints only while something changed on the orb and only on the
// UI task; it returns true when it painted. `live`/`proximity` set the idle glow (see dead_air_fx.hpp).
void fx_event(const Event& event, uint32_t now_ms);
void fx_idle(bool live, float proximity);
bool draw_fx(uint32_t now_ms);
Action handle_touch(int32_t x, int32_t y, uint32_t now_ms);
bool self_check();

} // namespace orcsdr::dead_air

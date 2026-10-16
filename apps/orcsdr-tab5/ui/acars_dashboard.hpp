#pragma once
// ACARS dashboard renderer (M5GFX). Draws the shared OrcSDR header, a context strip, six views
// (LIVE, AIRCRAFT, CHANNELS, ACTIVITY, ARCHIVE, SETUP), the message detail page and the tab bar.
// It owns no data: it reads a Model snapshot and a Controller, and never touches USB, the tuner,
// the decoder or the SD card. Behaviour lives in acars_dashboard_ui (host-tested).
#include <cstdint>

#include "acars_dashboard_model.hpp"
#include "acars_dashboard_ui.hpp"

namespace orcsdr::acars_dashboard {

// The shared battery / home / mute / visualizer / settings controls, drawn by the application after the
// dashboard's own brand (the same hook every OrcSDR dashboard uses).
using HeaderHook = void (*)();

void install(acars_ui::Model* model, acars_ui::Controller* controller, HeaderHook header_controls);
// Optional: returns true while a shared-header overlay (the volume tray) is open over the content. Repaints
// are deferred meanwhile so the tray is not erased.
void set_overlay_probe(bool (*overlay_open)());
void enter();                 // first draw after the ScreenController transition
void leave();
void draw();                  // full redraw (screen restored from Settings / Visualizer)
// Repaints only what changed since the last draw (UI revision or snapshot revision), at most every
// 250 ms. Does nothing when nothing changed: no polling load, no animation.
void update(uint32_t now_ms);
bool active();

// Cost of the last paints (microseconds on the device; pixels pushed to the panel). Diagnostics only.
struct PaintStats {
  uint32_t frames = 0;
  uint32_t whole_pushes = 0;      // frames pushed whole (first frame after enter/restore, or most of the screen changed)
  uint32_t partial_pushes = 0;    // frames pushed as changed tiles only
  uint32_t rects = 0;             // rectangles pushed by the last frame
  uint32_t pixels = 0;            // pixels pushed by the last frame
  uint32_t compose_us = 0, compose_us_max = 0;
  uint32_t diff_us = 0, diff_us_max = 0;
  uint32_t push_us = 0, push_us_max = 0;
};
PaintStats paint_stats();

// Explicit UI regression only (never at boot): exercises every tab and checks view state.
bool interaction_check();

}  // namespace orcsdr::acars_dashboard

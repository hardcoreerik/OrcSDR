#pragma once
// Application glue between the Tab5 Home screen module (unchanged source) and this board:
// builds its Snapshot, turns touches into its gestures, carries out its Actions, feeds it the
// spectrum, and presents the frame. The Tab5's main.cpp does this job on the Tab5; this is the
// P4 equivalent, kept small and separate (nothing here belongs in a shared dashboard).
#include <cstdint>
#include "dashboard_registry.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "orcsdr_gfx/scaled_gfx.hpp"

namespace home_app {
// Starts the UI task. `display` must already be begun and presenting.
bool start(orcsdr::gfx::ScaledDisplay* display);

// What the serial interpreter reports (a copy; fields are updated by the UI task).
struct Status {
  uint32_t frequency_hz = 0;      // tuned / requested frequency
  uint32_t applied_hz = 0;        // frequency the receiver reports (0 while not streaming)
  uint32_t step_hz = 0;
  uint32_t span_hz = 0;
  uint8_t volume = 0;             // 0-255
  bool sound = true;
  char band[24] = {};
  char mode[12] = {};
  uint32_t visible_updates = 0;   // frames presented so far
};
Status status();

// Requests from other tasks; applied by the UI task on its next loop.
void request_frequency(uint32_t hz);
void request_volume(uint8_t volume_0_255);
void request_sound(bool enabled);
void request_span(uint32_t span_hz);        // clamped to what the IQ stream can show
// Screen switching (dashboards call these from their adapters; applied on the UI task's next loop).
void request_open(orcsdr::dashboards::Id id);
void request_home();
// A tap at full-screen design coordinates (0-1279, 0-719), routed exactly like a finger, held ~150 ms.
void inject_tap(int design_x, int design_y);
// The screen name for RTL_UI STATUS ("home"). Returns false for a screen this target does not have.
bool open_screen(const char* name);
const char* screen_name();
}  // namespace home_app

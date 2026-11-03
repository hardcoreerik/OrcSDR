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
#include "home_web_actions.hpp"

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
  bool home_active = false;
  bool ui_ready = false,ui_failed = false;
  bool step_adjustable = false,mode_selectable = false;
  uint32_t filter_bandwidth_hz = 0;
  uint8_t mode_choice = 0,mode_active = 0,mode_suggested = 0;
  uint32_t web_action_sequence = 0;
  uint32_t web_action_id = 0;
  WebAction web_action_result = WebAction::not_available;
};
Status status();
// Success acknowledges queue admission, not application. No device audio or
// screen actions. The UI owner revalidates capabilities before native dispatch.
WebAction request_web_action(const char* name,uint32_t value,uint32_t* request_id=nullptr);
struct ActionReceipt {uint32_t request_id=0;WebAction dispatch_result=WebAction::not_available;};
// Last eight completed dispatches in this boot. False means pending/expired/
// unknown; it must never be interpreted as proof of successful application.
bool web_action_receipt(uint32_t request_id,ActionReceipt* out);

// Requests from other tasks; applied by the UI task on its next loop.
void request_frequency(uint32_t hz);
void request_volume(uint8_t volume_0_255);
void request_sound(bool enabled);
void request_span(uint32_t span_hz);        // clamped to what the IQ stream can show
// Screen switching (dashboards call these from their adapters; applied on the UI task's next loop).
void request_open(orcsdr::dashboards::Id id);
void request_home();
// Named requests use the same UI-owner transition as Settings/auto-rotate.
bool request_rotation(int degrees);
// Queues one screenshot for capture/streaming by the UI owner after a frame is presented.
bool request_screenshot();
int rotation();
int viewport_width();
int viewport_height();
// A tap at full-screen design coordinates (0-1279, 0-719), routed exactly like a finger, held ~150 ms.
void inject_tap(int design_x, int design_y);
// The screen name for RTL_UI STATUS ("home"). Returns false for a screen this target does not have.
bool open_screen(const char* name);
const char* screen_name();
}  // namespace home_app

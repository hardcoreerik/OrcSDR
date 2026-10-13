#pragma once
// ACARS application glue: owns the dashboard's lifecycle, its PSRAM-backed model and controller, and the
// producer seam a future receiver adapter will use. main.cpp supplies only what it alone can reach
// (the shared header controls, the preferences store, mute state) through Hooks and forwards five calls:
// open(), close(), tick(), draw() and touch().
#include <cstdint>

#include "acars_dashboard_model.hpp"

namespace orcsdr::acars_app {

struct Hooks {
  void (*header_controls)() = nullptr;               // draws the shared battery / home / mute / viz / settings
  bool (*sound_enabled)() = nullptr;                 // user mute state (shown by the shared header)
  // A request to retune. Null today: no receiver runtime exists, so a channel choice stays a request and the
  // dashboard says "NOT TUNED". A future runtime sets this and later calls Model::report_tuned().
  void (*request_tune)(uint32_t frequency_hz) = nullptr;
  acars_ui::Store store;                             // persistent UI preferences
};

void install(const Hooks& hooks);   // call before open(); safe to repeat
void open();                        // screen opened: model, preferences, first draw
void close();                       // left for any reason; no-op when not open. Leaves DEMO mode.
void tick();                        // main loop: drains the decoder->UI queue, repaints on change
void draw(bool static_panel);       // screen refresh requested by the ScreenController
void touch(int32_t x, int32_t y);   // touch routing for taps below the shared header
bool active();

// Producer seam for the receiver adapter (decoder task -> UI). Null until the first open(); a runtime must
// only use post_message / post_stats / post_receiver_state / post_capabilities on it (lock-free, no UI calls).
acars_ui::Model* model();

}  // namespace orcsdr::acars_app

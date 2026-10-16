#pragma once
// ACARS application glue: owns the dashboard's lifecycle, its PSRAM-backed model and controller, and the
// producer seam the receiver adapter (acars_receiver) uses. main.cpp supplies only what it alone can reach
// (the shared header controls, the preferences store, mute state) through Hooks and forwards five calls:
// open(), close(), tick(), draw() and touch().
#include <cstddef>
#include <cstdint>

#include "acars_archive.hpp"
#include "acars_dashboard_model.hpp"

namespace orcsdr::acars_app {

struct Hooks {
  void (*header_controls)() = nullptr;               // draws the shared battery / home / mute / viz / settings
  bool (*sound_enabled)() = nullptr;                 // user mute state (shown by the shared header)
  bool (*overlay_open)() = nullptr;                  // the shared volume tray is open over the content
  // Optional extra observer of a channel choice. The receiver (acars_receiver) is retuned by the app itself.
  void (*request_tune)(uint32_t frequency_hz) = nullptr;
  acars_ui::Store store;                             // persistent UI preferences
  const acars_archive::FileOps* archive_ops = nullptr;   // SD file operations; null = no saving (the archive then holds this session only)
};

void install(const Hooks& hooks);   // call before open(); safe to repeat
void open();                        // screen opened: model, preferences, first draw
void close();                       // left for any reason; no-op when not open. Leaves DEMO mode.
void tick();                        // main loop: drains the decoder->UI queue, repaints on change
void draw(bool static_panel);       // screen refresh requested by the ScreenController
void touch(int32_t x, int32_t y);   // touch routing for taps below the shared header
bool active();
// One diagnostic line for the serial console: view, demo state, receiver and tune state, message counts.
void describe(char* out, size_t size);

// Producer seam for the receiver adapter (decoder task -> UI). Null until the first open(); acars_receiver
// only uses post_message / post_stats / post_receiver_state / post_capabilities on it (lock-free, no UI calls).
acars_ui::Model* model();

}  // namespace orcsdr::acars_app

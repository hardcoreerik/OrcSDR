#pragma once
// AIS application glue: owns the dashboard's lifecycle and the UI-side snapshot, and starts / stops the radio adapter
// (ais_radio). The firmware supplies what only it can reach through Hooks and forwards five calls:
// open(), close(), tick(), draw() and touch(). The dashboard never owns the radio or the decoder.
#include <cstddef>
#include <cstdint>

namespace orcsdr::ais_app {

struct Hooks {
  void (*header_controls)() = nullptr;      // draws the shared battery / home / mute / visualizer / settings controls
  int (*battery_percent)() = nullptr;       // -1 when unknown
  // Configured receiver position; false when the user has not set one (targets still show, the map centres on them).
  bool (*receiver_location)(int32_t* lat_e7, int32_t* lon_e7) = nullptr;
  bool (*sd_available)() = nullptr;
  bool (*remote_source)() = nullptr;        // the receiver source is rtl_tcp
  uint32_t (*now_ms)() = nullptr;
  void (*go_home)() = nullptr;              // used when the dashboard cannot be drawn (no memory): never leave an undrawn screen
};

void install(const Hooks& hooks);   // call before open(); safe to repeat
void open();                        // screen opened: snapshot memory, receiver start, first draw
void close();                       // left for any reason; stops the receiver and frees everything. No-op when not open.
void tick();                        // main loop: receiver service, newest published feed, repaint on change
void draw(bool static_panel);       // screen refresh requested by the ScreenController (true = full redraw)
void touch(int32_t x, int32_t y);   // taps below the shared header
bool active();
void describe(char* out, size_t size);   // one "RTL_AIS_UI ..." line

}  // namespace orcsdr::ais_app

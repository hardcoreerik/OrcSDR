#pragma once
// Digital Voice application glue. Owns the dashboard lifecycle and the UI-side snapshot, nothing else.
// It never owns the radio: receiver state comes from the firmware through Hooks, and scan/hold/skip/save actions
// the dashboard returns are NOT applied until the scanner (scan_engine + radio session) is wired in.
// main.cpp forwards six calls only: open(), close(), tick(), draw(), touch(), active().
#include <cstddef>
#include <cstdint>

namespace orcsdr::digital_voice_app {

struct Hooks {
  void (*header_controls)() = nullptr;   // shared battery / home / volume / visualizer / settings controls
  bool (*overlay_open)() = nullptr;      // shared volume tray is open over the content: do not paint under it
  bool (*receiver_ready)() = nullptr;    // an RTL-SDR is present and streaming
  void (*go_home)() = nullptr;           // memory failure: never leave an undrawn screen
  uint32_t (*now_ms)() = nullptr;
};

void install(const Hooks& hooks);
void open();                       // screen opened: allocate PSRAM state, first full draw
void close();                      // left for any reason; frees everything. No-op when not open.
void tick();                       // main loop: refresh the snapshot, repaint on change
void draw(bool static_panel);      // ScreenController refresh (true = full redraw after Settings/Visualizer)
void touch(int32_t x, int32_t y);  // taps below the shared header
bool active();
void describe(char* out, size_t size);   // one "RTL_DV_UI ..." serial line

}  // namespace orcsdr::digital_voice_app

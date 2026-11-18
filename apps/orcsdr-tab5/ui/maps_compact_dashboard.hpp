#pragma once
// Compact MAPS screen (maps_compact_dashboard.cpp) for views smaller than 1280 x 720: the map packs found on the card, paged, with a
// rescan. The Tab5's full map_screen.cpp is separate. Not on this view yet: the location picker and the download page.
#include "maps_compact_layout.hpp"

namespace orcsdr::maps_ui {
enum class Action : uint8_t { none, rescan };
void set_header_controls(void (*draw)());    // the shared home/mute/settings controls (the application's header hook)
bool set_viewport(compact::Rect viewport);   // validates the view, moves the shared header, allocates the (PSRAM) state
bool enter();                                // draws the first frame
void update();                               // redraws when the pack list changed
void leave();
Action touch(int x, int y);
void draw();                                 // full redraw (geometry change)
}  // namespace orcsdr::maps_ui

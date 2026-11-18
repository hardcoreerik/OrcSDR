#pragma once
// Compact DEAD AIR dashboard (dead_air_compact_dashboard.cpp): the orcsdr::dead_air dashboard contract of dead_air_dashboard.hpp for
// views smaller than 1280 x 720, plus the viewport call the application makes before dead_air_app::open(). A build links either this or
// dead_air_dashboard.cpp. Pages: OVERVIEW, SPECTRUM, EVENTS, SETUP (reported as Page::review). Not on this view yet: the Audio, ENV and
// Review pages, the scrub / control-run / session-card / VEIL-26 views and the orb.
#include "dead_air_compact_layout.hpp"
#include "dead_air_dashboard.hpp"

namespace orcsdr::dead_air {
bool ui_set_viewport(compact::Rect viewport);        // validates the view, moves the shared header, allocates the (PSRAM) state
void ui_set_header_controls(void (*draw)());          // the shared home / mute / settings controls
}  // namespace orcsdr::dead_air

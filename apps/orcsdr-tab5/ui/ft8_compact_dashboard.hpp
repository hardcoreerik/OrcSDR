#pragma once
// Compact FT8 / FT4 dashboard for views smaller than the legacy 1280 x 720 (LilyGO T-Display P4 portrait and landscape).
// It takes the same ft8::Snapshot and returns the same ft8::Action as ft8_dashboard.cpp, so an application adapter drives
// either one. Tabs: LIVE (band, waterfall, slot timer, latest decodes), DECODES (paged list), SETUP (mode, clock).
// The legacy dashboard's MAP, HUNTER and HEARD tabs and its expert tuning panel are not on this view yet.
#include "ft8_compact_layout.hpp"
#include "ft8_dashboard.hpp"

namespace orcsdr::ft8::compact {

enum class Tab : uint8_t { live, decodes, setup };

// Validates the view and installs it (also moves the shared header). False leaves the previous view in place.
bool set_viewport(Rect viewport);
// The application draws the shared header controls (Home, volume, battery, Settings); same hook as the legacy dashboard.
void set_header_hook(void (*draw_controls)());
void enter(const Snapshot& snapshot);
void update(const Snapshot& snapshot);
void draw();                       // full repaint of the view
Action handle_touch(int32_t x, int32_t y);
void leave();
bool active();
Tab tab();
void select_tab(Tab tab);
size_t decode_page();

}  // namespace orcsdr::ft8::compact

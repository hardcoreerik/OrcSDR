#pragma once
// Copies the scaled display's changed rows into the panel's frame buffers and flips them.
#include "hosyond_board.h"
#include "orcsdr_gfx/scaled_gfx.hpp"

// Call once after the display sprite exists. Detects the sprite's RGB565 byte order.
bool present_init(orcsdr::gfx::ScaledDisplay* display, const hosyond_display_t* panel);
// Headless: no panel is attached. The UI still renders into the scaled display's sprite (screenshots over
// serial read it), present_frame() consumes the dirty rows, and nothing is converted or flipped.
bool present_init_headless(orcsdr::gfx::ScaledDisplay* display);
// Converts and shows whatever changed since the last call. Returns the number of rows copied (0 = nothing).
int present_frame();
// Statistics for diagnostics: microseconds spent in the last non-empty present.
unsigned present_last_us();

// UI-owner geometry, shared with physical touch mapping. Null in headless mode.
const orcsdr_display_geometry_t* present_geometry();

// UI-owner only; preserves native scanout buffers, reallocates only the view sprite.
bool present_set_rotation(int degrees);

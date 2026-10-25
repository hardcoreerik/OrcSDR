#pragma once
// Copies the scaled display's changed rows into the panel's frame buffers and flips them.
#include "hosyond_board.h"
#include "orcsdr_gfx/scaled_gfx.hpp"

// Call once after the display sprite exists. Detects the sprite's RGB565 byte order.
bool present_init(orcsdr::gfx::ScaledDisplay* display, const hosyond_display_t* panel);
// Converts and shows whatever changed since the last call. Returns the number of rows copied (0 = nothing).
int present_frame();
// Statistics for diagnostics: microseconds spent in the last non-empty present.
unsigned present_last_us();

#pragma once

#include "focus_nav.hpp"

// Draws the keyboard focus ring around a control and puts back the pixels underneath when focus
// moves, so navigating does not repaint the screen.
namespace orcsdr::focus_ring {

// Shows the ring around `rect`, first removing the previous one.
void show(const focus_nav::Rect& rect);

// Removes the ring, restoring what it covered.
void hide();

// The screen was repainted (or changed): the saved pixels are stale, so just forget the ring.
void forget();

bool visible();

}  // namespace orcsdr::focus_ring

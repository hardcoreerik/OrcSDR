#pragma once
#include "orcsdr_gfx/scaled_gfx.hpp"
// Streams the current screen as a PNG in the ORC_SNAP_* format (see screen_snap.cpp).
bool screen_snap_stream(orcsdr::gfx::ScaledDisplay* display);

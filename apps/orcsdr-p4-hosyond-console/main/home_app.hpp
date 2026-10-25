#pragma once
// Application glue between the Tab5 Home screen module (unchanged source) and this board:
// builds its Snapshot, turns touches into its gestures, carries out its Actions, feeds it the
// spectrum, and presents the frame. The Tab5's main.cpp does this job on the Tab5; this is the
// P4 equivalent, kept small and separate (nothing here belongs in a shared dashboard).
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "orcsdr_gfx/scaled_gfx.hpp"

namespace home_app {
// Starts the UI task. `display` must already be begun and presenting.
bool start(orcsdr::gfx::ScaledDisplay* display);
}  // namespace home_app

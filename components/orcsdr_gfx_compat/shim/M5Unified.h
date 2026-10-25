#pragma once
// Stand-in for M5Unified on the Waveshare P4 build: the same `M5` object and `M5Canvas` type the
// Tab5 sources use, backed by the scaled M5GFX front end (see orcsdr_gfx/scaled_gfx.hpp).
#include "orcsdr_gfx/m5_stubs.hpp"

using M5Canvas = orcsdr::gfx::ScaledCanvas;
extern orcsdr::gfx::M5Class M5;
namespace m5 {
using rtc_datetime_t = orcsdr::gfx::rtc_datetime_t;
}

#pragma once
// Stand-in for M5Unified on the Waveshare P4 build: the same `M5` object and `M5Canvas` type the
// Tab5 sources use, backed by the scaled M5GFX front end (see orcsdr_gfx/scaled_gfx.hpp).
#include "orcsdr_gfx/m5_stubs.hpp"

using M5Canvas = orcsdr::gfx::ScaledCanvas;
extern orcsdr::gfx::M5Class M5;
namespace m5 {
using rtc_datetime_t = orcsdr::gfx::rtc_datetime_t;
}

// A few Tab5 sources (ACARS, FT8, Dead Air) hold their drawing target as `lgfx::LovyanGFX*` and draw through it.
// For just those files (compile definition ORCSDR_LOVYANGFX_IS_SCALED) that name means the scaled front end, so
// the same code draws scaled on this board. Everything after this header sees the alias.
#ifdef ORCSDR_NO_RAW_BUFFERS
#define getBuffer getBufferUnavailable
#endif

#ifdef ORCSDR_LOVYANGFX_IS_SCALED
namespace lgfx {
using ScaledGfxAlias = ::orcsdr::gfx::ScaledGfx;
}
#define LovyanGFX ScaledGfxAlias
#endif

#pragma once
// RTL_DISPLAY_BENCH: times ways of copying a Home-sized spectrum picture (904 x 184, the Home trace sprite) onto the
// rotated Tab5 framebuffer, and reads the DSP load while each one runs. Measurement only.
//
// Everything is drawn into the second framebuffer, which is not on screen unless the RF visualizer is page-flipping
// (then the bench refuses), so the visible screen does not change. The main loop is busy for the whole run (about
// 4 x `seconds` plus a few checks): touch and screen updates pause, audio and radio keep running.
//
// Methods, each run back to back for `seconds`:
//   idle    nothing drawn: the DSP load with no screen copying at all
//   m5gfx   M5GFX pushSprite onto a rotated canvas over the framebuffer (the per-pixel path screens use today)
//   tiled   display_rotate::rotate_copy (CPU, 32 x 32 tiles)
//   ppa     the P4's PPA scale-rotate-mirror engine (DMA), after finding the rotation that matches m5gfx exactly
// tiled and ppa are compared pixel by pixel with m5gfx before they are timed.
namespace orcsdr::display_bench {

using Emit = void (*)(const char* line);

void run(unsigned seconds, bool visualizer_active, Emit emit);

}  // namespace orcsdr::display_bench

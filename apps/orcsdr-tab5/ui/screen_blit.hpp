#pragma once
// Copies a finished 16-bit sprite to the Tab5 screen with the P4's PPA engine instead of M5GFX's per-pixel rotated
// copy. Same pixels in the same place (RTL_DISPLAY_BENCH checks it pixel by pixel); the difference is that the copy is
// done by DMA, which measured 12 ms instead of 18.5 ms for the Home spectrum and did not slow the DSP (46% load
// against 61%).
//
// push() falls back to sprite->pushSprite() whenever the hardware copy cannot reproduce M5GFX exactly: a screen
// rotation other than the two landscape ones, a sprite that is not plain 16-bit, a rectangle that leaves the screen,
// or any PPA error. Main loop only.
//
// Cache rule: the PPA driver discards the CPU's cached copy of the framebuffer rows it writes. push() first writes
// those rows back from the cache, so pixels other code drew next to the sprite are never lost.
namespace m5gfx {
class M5Canvas;
}

namespace orcsdr::screen_blit {

void push(m5gfx::M5Canvas* sprite, int x, int y);

// RTL_DISPLAY_BENCH and diagnostics: copies done by the PPA, copies that fell back to M5GFX, and the last PPA error.
struct Counters {
  unsigned long ppa = 0;
  unsigned long fallback = 0;
  int last_error = 0;
};
Counters counters();

}  // namespace orcsdr::screen_blit

#pragma once
// Screen work done by the P4's PPA engine (DMA) instead of the CPU, for screens that redraw large areas every frame.
// Measured on a Tab5 (RTL_DISPLAY_BENCH): M5GFX's per-pixel rotated copy of the Home spectrum took 18.5 ms and raised
// the DSP load on the other core from 45% to 61%; the PPA copy took 12 ms and left it at 46%.
//
//   push()       a finished 16-bit sprite onto the rotated screen
//   scroll_up()  a screen rectangle moved up by `rows` (M5GFX setScrollRect + scroll(0, -rows)), bottom rows filled
//   DmaSprite    a sprite whose buffer the PPA can clear (Home's spectrum picture)
//
// Each one gives the same pixels as M5GFX (checked pixel by pixel by RTL_DISPLAY_BENCH for both landscape rotations)
// and falls back to M5GFX for anything it cannot reproduce exactly: another rotation, another colour depth, a
// rectangle that leaves the screen, a buffer the PPA cannot use, or any PPA error. Main loop only.
//
// Cache rule: the PPA driver discards the CPU's cached copy of the framebuffer rows it writes, so these first write
// those rows back from the cache; pixels other code drew next to the target are never lost.
#include <cstddef>
#include <cstdint>

#include "display_rotate.hpp"

namespace m5gfx {
class M5Canvas;
}

namespace orcsdr::screen_blit {

void push(m5gfx::M5Canvas* sprite, int x, int y);
void scroll_up(int x, int y, int w, int h, int rows, uint16_t fill_color);

// Like scope::Trace (scope_canvas.hpp), but its buffer is aligned for the PPA, which clears it when the background is
// black. begin() returns nullptr when there is no memory.
class DmaSprite {
 public:
  DmaSprite() = default;
  ~DmaSprite();
  DmaSprite(const DmaSprite&) = delete;
  DmaSprite& operator=(const DmaSprite&) = delete;
  m5gfx::M5Canvas* begin(int width, int height, uint16_t background);

 private:
  m5gfx::M5Canvas* canvas_ = nullptr;
  void* buffer_ = nullptr;
  size_t bytes_ = 0;
  int width_ = 0;
  int height_ = 0;
};

// The same operations on any framebuffer: the live screen uses M5.Display's, RTL_DISPLAY_BENCH the hidden one.
struct Target {
  uint16_t* fb = nullptr;
  display_rotate::Geometry g;
};
bool live_target(Target* out);
int ppa_push(const Target& t, const void* pixels, int w, int h, int x, int y);   // esp_err_t
int ppa_scroll_up(const Target& t, int x, int y, int w, int h, int rows);       // moves the content only
int ppa_clear(void* buffer, size_t bytes, int w, int h);                         // to black (0x0000)

struct Counters {
  unsigned long push_ppa = 0, push_fallback = 0;
  unsigned long scroll_ppa = 0, scroll_fallback = 0;
  unsigned long clear_ppa = 0, clear_fallback = 0;
  int last_error = 0;
};
Counters counters();

}  // namespace orcsdr::screen_blit

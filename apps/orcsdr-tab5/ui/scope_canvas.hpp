#pragma once

#include <M5Unified.h>

#include <cstdint>

// Shared drawing helpers for the live spectrum scopes (FM, Shortwave, AM, CB, P25).
//
// Every scope used to erase its plot area on the display and redraw it each frame, which flickered and cost
// time. The trace is now drawn into an off-screen sprite in PSRAM and pushed in one go, and the waterfall is
// scrolled by hardware two rows at a time. The scopes refresh every 50 ms (see kRtlLoraSpectrumIntervalMs).
namespace orcsdr::scope {

// An off-screen plot area. begin() hands back a cleared sprite of the requested size (nullptr if there is no
// memory for it, in which case the caller skips drawing the trace). The sprite is rebuilt only when the size
// changes, e.g. when a side drawer narrows the plot.
class Trace {
 public:
  M5Canvas* begin(int width, int height, uint16_t background) {
    if (canvas_ != nullptr && (width_ != width || height_ != height)) {
      delete canvas_;
      canvas_ = nullptr;
    }
    if (canvas_ == nullptr) {
      canvas_ = new M5Canvas(&M5.Display);
      canvas_->setPsram(true);
      canvas_->setColorDepth(16);
      if (!canvas_->createSprite(width, height)) {
        delete canvas_;
        canvas_ = nullptr;
      }
      width_ = width;
      height_ = height;
    }
    if (canvas_ != nullptr) canvas_->fillSprite(background);
    return canvas_;
  }

 private:
  M5Canvas* canvas_ = nullptr;
  int width_ = 0;
  int height_ = 0;
};

// Scrolls the waterfall up by `rows` and writes the newest colour row into the freed lines. `last_row_y` is
// the screen y of the bottom-most waterfall line the dashboard paints.
inline void scroll_waterfall(int x, int last_row_y, int width, const uint16_t* colors, int rows = 2) {
  M5.Display.startWrite();
  M5.Display.scroll(0, -rows);
  for (int r = 0; r < rows; ++r)
    M5.Display.pushImage(x, last_row_y - rows + 1 + r, width, 1, colors);
  M5.Display.endWrite();
}

// Frames drawn in the last second and the time the last frame took, for the status line.
class FrameStats {
 public:
  void frame_done(uint32_t started_ms) {
    const uint32_t now = millis();
    draw_ms_ = now - started_ms;
    ++frames_;
    if (started_ms - window_ms_ >= 1000u) {
      fps_ = frames_;
      frames_ = 0;
      window_ms_ = started_ms;
    }
  }
  uint32_t fps() const { return fps_; }
  uint32_t draw_ms() const { return draw_ms_; }

 private:
  uint32_t fps_ = 0, frames_ = 0, window_ms_ = 0, draw_ms_ = 0;
};

}  // namespace orcsdr::scope

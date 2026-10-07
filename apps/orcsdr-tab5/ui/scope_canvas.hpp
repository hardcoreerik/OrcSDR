#pragma once

#include "waterfall_style.hpp"

#include <M5Unified.h>

#include <cstdint>
#include <cstdio>

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
    // M5GFX reads a uint16_t* image as big-endian RGB565, but the waterfall colours are native RGB565 (what fillRect and the sprites use), so push the row through the rgb565_t overload or every hue is wrong.
    M5.Display.pushImage(x, last_row_y - rows + 1 + r, width, 1,
                         reinterpret_cast<const lgfx::rgb565_t*>(colors));
  M5.Display.endWrite();
}

// Waterfall palette and speed chips in the top-right corner of the plot, drawn into the trace sprite so they
// refresh with it. Tapping a chip cycles that setting; the choice is stored per screen (waterfall_style).
constexpr int kChipW = 128;
constexpr int kChipH = 26;
constexpr int kChipGap = 6;
constexpr int kChipMargin = 6;

inline int chip_x(int plot_w, int index) {   // index 0 = palette (left), 1 = speed (right)
  return plot_w - kChipMargin - 2 * kChipW - kChipGap + index * (kChipW + kChipGap);
}

inline void draw_style_chips(M5Canvas* canvas, int plot_w, waterfall_style::Screen screen) {
  if (canvas == nullptr) return;
  char label[24];
  for (int i = 0; i < 2; ++i) {
    const int x = chip_x(plot_w, i);
    std::snprintf(label, sizeof(label), i == 0 ? "PAL %s" : "SPD %s",
                  i == 0 ? waterfall_style::palette_name(waterfall_style::palette(screen))
                         : waterfall_style::speed_name(waterfall_style::speed(screen)));
    canvas->fillRoundRect(x, kChipMargin, kChipW, kChipH, 5, 0x0000);
    canvas->drawRoundRect(x, kChipMargin, kChipW, kChipH, 5, 0x07FF);
    canvas->setFont(&fonts::Font2);
    canvas->setTextColor(0x07FF, 0x0000);
    canvas->setTextDatum(middle_center);
    canvas->drawString(label, x + kChipW / 2, kChipMargin + kChipH / 2);
  }
}

// A tap at screen (x, y): true when it landed on a chip, which has then been cycled. `plot_left` and
// `plot_top` are the screen position of the sprite and `plot_w` its width.
inline bool style_chip_tap(int x, int y, int plot_left, int plot_top, int plot_w,
                           waterfall_style::Screen screen) {
  if (y < plot_top + kChipMargin || y >= plot_top + kChipMargin + kChipH) return false;
  for (int i = 0; i < 2; ++i) {
    const int left = plot_left + chip_x(plot_w, i);
    if (x >= left && x < left + kChipW) {
      if (i == 0) waterfall_style::next_palette(screen);
      else waterfall_style::next_speed(screen);
      return true;
    }
  }
  return false;
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

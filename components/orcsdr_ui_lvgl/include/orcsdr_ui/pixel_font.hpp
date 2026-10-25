#pragma once
// The OrcSDR UI draws its text with M5GFX's built-in Font0 (the classic Adafruit 5x7 font in
// 6x8 cells) at integer pixel scales. This reproduces that font for LVGL so the look matches:
// pixel_font(c) is Font0 with every font pixel drawn as a c x c block.
#include "lvgl.h"
namespace orcsdr::ui {
// cell 1..6. Cell sizes are 6c x 8c per character. Returns nullptr for an unsupported cell.
const lv_font_t* pixel_font(int cell);
// Width in pixels of `chars` characters at `cell`.
constexpr int pixel_text_width(int chars, int cell) { return chars * 6 * cell; }
}  // namespace orcsdr::ui

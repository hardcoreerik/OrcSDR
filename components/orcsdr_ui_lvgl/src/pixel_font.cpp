// LVGL custom font reproducing M5GFX Font0 (Adafruit "classic" 5x7 in a 6x8 cell).
// Glyph source: glcdfont.h (BSD, Adafruit Industries; license text kept in that file).
#include "orcsdr_ui/pixel_font.hpp"
#include <cstdint>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"
#ifndef PROGMEM
#define PROGMEM
#endif
#include "glcdfont.h"

namespace orcsdr::ui {
namespace {
constexpr int kMaxCell = 6;
constexpr uint32_t kFirst = 32, kLast = 126;  // printable ASCII; everything else shows '?'

struct FontSet {
  lv_font_t font{};
  int cell = 0;
  uint8_t* bitmaps = nullptr;  // (kLast-kFirst+1) glyphs, A8, (5*cell) x (8*cell) each
  size_t glyph_bytes = 0;
};
FontSet sets[kMaxCell + 1];

uint32_t map_letter(uint32_t letter) { return (letter < kFirst || letter > kLast) ? '?' : letter; }

bool get_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc, uint32_t letter, uint32_t) {
  const auto* set = static_cast<const FontSet*>(font->user_data);
  if (letter == 0) return false;
  const uint32_t mapped = map_letter(letter);
  dsc->resolved_font = font;
  dsc->adv_w = static_cast<uint16_t>(6 * set->cell);
  dsc->box_w = static_cast<uint16_t>(5 * set->cell);
  dsc->box_h = static_cast<uint16_t>(8 * set->cell);
  dsc->ofs_x = 0;
  dsc->ofs_y = 0;
  dsc->format = LV_FONT_GLYPH_FORMAT_A8;
  dsc->is_placeholder = 0;
  dsc->gid.index = mapped;
  return true;
}

const void* get_glyph_bitmap(lv_font_glyph_dsc_t* dsc, lv_draw_buf_t*) {
  const auto* set = static_cast<const FontSet*>(dsc->resolved_font->user_data);
  return set->bitmaps + static_cast<size_t>(dsc->gid.index - kFirst) * set->glyph_bytes;
}

bool build(int cell) {
  FontSet& set = sets[cell];
  if (set.bitmaps) return true;
  const int w = 5 * cell, h = 8 * cell;
  set.glyph_bytes = static_cast<size_t>(w) * h;
  const size_t total = set.glyph_bytes * (kLast - kFirst + 1);
  set.bitmaps = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!set.bitmaps) return false;
  for (uint32_t ch = kFirst; ch <= kLast; ++ch) {
    uint8_t* out = set.bitmaps + (ch - kFirst) * set.glyph_bytes;
    for (int col = 0; col < 5; ++col) {
      const uint8_t bits = font[ch * 5 + col];  // bit 0 is the top row
      for (int row = 0; row < 8; ++row) {
        const uint8_t v = ((bits >> row) & 1) ? 255 : 0;
        for (int dy = 0; dy < cell; ++dy)
          std::memset(out + static_cast<size_t>(row * cell + dy) * w + col * cell, v, cell);
      }
    }
  }
  set.cell = cell;
  lv_font_t& f = set.font;
  f.get_glyph_dsc = get_glyph_dsc;
  f.get_glyph_bitmap = get_glyph_bitmap;
  f.release_glyph = nullptr;
  f.line_height = h;
  f.base_line = 0;
  f.subpx = LV_FONT_SUBPX_NONE;
  f.kerning = LV_FONT_KERNING_NONE;
  f.static_bitmap = 1;
  f.underline_position = -1;
  f.underline_thickness = static_cast<int8_t>(cell);
  f.dsc = nullptr;
  f.fallback = nullptr;
  f.user_data = &set;
  return true;
}
}  // namespace

const lv_font_t* pixel_font(int cell) {
  if (cell < 1 || cell > kMaxCell) return nullptr;
  if (!build(cell)) {
    ESP_LOGE("PIXEL_FONT", "out of memory for cell %d", cell);
    return nullptr;
  }
  return &sets[cell].font;
}
}  // namespace orcsdr::ui

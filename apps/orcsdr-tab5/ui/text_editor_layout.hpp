#pragma once

#include <algorithm>
#include <cstddef>

namespace orcsdr::text_editor {

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

struct Layout {
  Rect viewport, title, field, keyboard;
  Rect shift, symbols, space, back, reveal, cancel, accept;
  Rect keys[4][12]{};
  std::size_t key_counts[4]{};
  int visual_rows = 0;
  bool legacy = false;
  bool valid = false;
};

inline bool inside(Rect outer, Rect inner) {
  return inner.w > 0 && inner.h > 0 && inner.x >= outer.x && inner.y >= outer.y &&
         inner.x - outer.x <= outer.w - inner.w &&
         inner.y - outer.y <= outer.h - inner.h;
}

inline Layout make_layout(Rect viewport, bool symbols = false) {
  Layout layout{};
  layout.viewport = viewport;
  if (viewport.x == 0 && viewport.y == 72 && viewport.w == 1280 && viewport.h == 648) {
    constexpr int left = 20, width = 1240, key_gap = 8;
    layout.legacy = true;
    layout.title = {left, 100, width, 40};
    layout.field = {left, 124, width, 64};
    layout.keyboard = {left, 202, width, 320};
    layout.shift = {left, 534, 190, 66};
    layout.symbols = {left + 205, 534, 210, 66};
    layout.space = {left + 430, 534, 370, 66};
    layout.back = {left + 815, 534, 200, 66};
    layout.reveal = {left + 1030, 534, 210, 66};
    layout.cancel = {left, 614, 320, 80};
    layout.accept = {left + width - 320, 614, 320, 80};
    const int normal_counts[] = {10, 10, 9, 7};
    const int symbol_counts[] = {10, 10, 12, 0};
    const int* counts = symbols ? symbol_counts : normal_counts;
    for (int r = 0; r < 4; ++r) {
      const int count = counts[r];
      layout.key_counts[r] = static_cast<std::size_t>(count);
      if (!count) continue;
      const int ten_width = (width - 9 * key_gap) / 10;
      const int key_width = std::min(ten_width, (width - (count - 1) * key_gap) / count);
      const int span = count * key_width + (count - 1) * key_gap;
      for (int c = 0; c < count; ++c)
        layout.keys[r][c] = {left + (width - span) / 2 + c * (key_width + key_gap),
                             202 + r * 82, key_width, 74};
    }
    layout.visual_rows = 4;
    layout.valid = true;
    return layout;
  }

  constexpr int margin = 8, gap = 4;
  if (viewport.w >= 800) {
    constexpr int title_h = 28, field_h = 48, key_h = 44, key_pitch = 48;
    constexpr int function_h = 44;
    const int left = viewport.x + margin;
    const int width = viewport.w - 2 * margin;
    layout.title = {left, viewport.y + 8, width, title_h};
    layout.field = {left, viewport.y + 40, width, field_h};
    layout.keyboard = {left, viewport.y + 96, width, key_pitch * 4 - gap};
    const int counts[] = {10, 10, symbols ? 12 : 9, symbols ? 0 : 7};
    for (int r = 0; r < 4; ++r) {
      const int count = counts[r];
      layout.key_counts[r] = static_cast<std::size_t>(count);
      if (!count) continue;
      const int ten_width = (width - 9 * gap) / 10;
      const int key_width = std::min(ten_width, (width - (count - 1) * gap) / count);
      const int span = count * key_width + (count - 1) * gap;
      for (int c = 0; c < count; ++c)
        layout.keys[r][c] = {left + (width - span) / 2 + c * (key_width + gap),
                             viewport.y + 96 + r * key_pitch, key_width, key_h};
    }
    layout.visual_rows = 4;

    const int toolbar_y = viewport.y + 296;
    const int toolbar_gap = 8;
    const int toolbar_width = width - 6 * toolbar_gap;
    const int weights[] = {10, 11, 22, 9, 9, 11, 11};
    constexpr int weight_sum = 83;
    int x = left;
    Rect* controls[] = {&layout.shift, &layout.symbols, &layout.space, &layout.back,
                        &layout.reveal, &layout.cancel, &layout.accept};
    int used = 0;
    for (int i = 0; i < 7; ++i) {
      const int w = i == 6 ? toolbar_width - used
                           : toolbar_width * weights[i] / weight_sum;
      *controls[i] = {x, toolbar_y, w, function_h};
      x += w + toolbar_gap;
      used += w;
    }
    layout.valid = viewport.h >= 340 && width >= 7 * 44 + 6 * toolbar_gap &&
                   inside(viewport, layout.title) && inside(viewport, layout.field) &&
                   inside(viewport, layout.keyboard) && inside(viewport, layout.accept);
  } else {
    constexpr int columns = 6, key_h = 64, key_pitch = 68;
    constexpr int function_h = 56, action_h = 56;
    const int left = viewport.x + margin;
    const int width = viewport.w - 2 * margin;
    const int key_width = (width - (columns - 1) * gap) / columns;
    layout.title = {left, viewport.y + 8, width, 32};
    layout.field = {left, viewport.y + 48, width, 56};
    layout.keyboard = {left, viewport.y + 112, width, key_pitch * 8 - gap};
    const int counts[] = {10, 10, symbols ? 12 : 9, symbols ? 0 : 7};
    for (int r = 0; r < 4; ++r) {
      const int count = counts[r];
      layout.key_counts[r] = static_cast<std::size_t>(count);
      const int chunks = (count + columns - 1) / columns;
      for (int c = 0; c < count; ++c) {
        const int visual_row = c / columns;
        const int visual_col = c % columns;
        const int this_row = std::min(columns, count - visual_row * columns);
        const int span = this_row * key_width + (this_row - 1) * gap;
        const int x = left + (width - span) / 2 + visual_col * (key_width + gap);
        const int y = viewport.y + 112 + (r * 2 + visual_row) * key_pitch;
        layout.keys[r][c] = {x, y, key_width, key_h};
      }
      layout.visual_rows += std::max(2, chunks);
    }
    const int function_y = viewport.y + 668;
    const int function_gap = 4;
    const int function_width = (width - 4 * function_gap) / 5;
    Rect* functions[] = {&layout.shift, &layout.symbols, &layout.space, &layout.back,
                         &layout.reveal};
    for (int i = 0; i < 5; ++i)
      *functions[i] = {left + i * (function_width + function_gap), function_y,
                       function_width, function_h};
    const int action_y = viewport.y + 732;
    const int action_gap = 8;
    const int action_width = (width - action_gap) / 2;
    layout.cancel = {left, action_y, action_width, action_h};
    layout.accept = {left + action_width + action_gap, action_y,
                     action_width, action_h};
    layout.valid = viewport.h >= 788 && key_width >= 44 && function_width >= 44 &&
                   inside(viewport, layout.title) && inside(viewport, layout.field) &&
                   inside(viewport, layout.keyboard) && inside(viewport, layout.accept);
  }

  if (layout.valid) {
    for (int r = 0; r < 4; ++r)
      for (std::size_t c = 0; c < layout.key_counts[r]; ++c)
        layout.valid = layout.valid && layout.keys[r][c].w >= 44 &&
                       layout.keys[r][c].h >= 44 && inside(viewport, layout.keys[r][c]);
    const Rect controls[] = {layout.shift, layout.symbols, layout.space, layout.back,
                             layout.reveal, layout.cancel, layout.accept};
    for (const auto& control : controls)
      layout.valid = layout.valid && control.w >= 44 && control.h >= 44 &&
                     inside(viewport, control);
  }
  return layout;
}

} // namespace orcsdr::text_editor

#pragma once

#include <algorithm>
#include <cstring>
#include "settings_layout.hpp"

namespace orcsdr::settings {

inline constexpr char kStorageNoteText[] =
    "Targeted deletion requires hold and confirmation; partitioning stays in M5Launcher.";

inline int storage_note_line_count(int width) {
  constexpr int conservative_glyph_width = 12;
  const int capacity = std::max(1, width / conservative_glyph_width);
  const auto length = std::strlen(kStorageNoteText);
  std::size_t offset = 0;
  int lines = 0;
  while (offset < length) {
    std::size_t count = std::min<std::size_t>(capacity, length - offset);
    if (offset + count < length && kStorageNoteText[offset + count] != ' ') {
      std::size_t split = count;
      while (split && kStorageNoteText[offset + split] != ' ') --split;
      if (split) count = split;
    }
    offset += count;
    while (offset < length && kStorageNoteText[offset] == ' ') ++offset;
    ++lines;
  }
  return lines;
}

struct StorageTextItem {
  Rect label;
  Rect value;
};

struct StorageLayout {
  Rect title;
  Rect note;
  StorageTextItem items[5];
  bool two_columns = false;
};

// Text rectangles are shared by storage drawing and its host geometry test.
inline StorageLayout make_storage_layout(Rect content) {
  StorageLayout layout{};
  layout.title = {content.x + 8, content.y + 8, content.w - 16, 28};
  layout.two_columns = content.w >= 800 && content.h < 320;

  if (layout.two_columns) {
    const int gap = 24;
    const int col_width = (content.w - 16 - gap) / 2;
    const int left_x = content.x + 8;
    const int right_x = left_x + col_width + gap;
    const int first_y = content.y + 36;
    const int step = 48;
    for (int i = 0; i < 5; ++i) {
      const bool right = i >= 3;
      const int local_row = right ? i - 3 : i;
      const int x = right ? right_x : left_x;
      const int y = first_y + local_row * step;
      const int width = col_width;
      layout.items[i] = {{x, y, width, 24}, {x, y + 24, width, 24}};
    }
    const int note_height = storage_note_line_count(content.w - 16) * 24;
    layout.note = {content.x + 8, content.y + content.h - note_height - 4,
                   content.w - 16, note_height};
  } else {
    const int x = content.x + 8;
    const int width = content.w - 16;
    const int first_y = content.y + 44;
    const int step = 52;
    for (int i = 0; i < 5; ++i) {
      const int y = first_y + i * step;
      layout.items[i] = {{x, y, width, 24}, {x, y + 24, width, 24}};
    }
    const int note_height = std::max(96, storage_note_line_count(width) * 24);
    layout.note = {x, content.y + content.h - note_height - 4,
                   width, note_height};
  }
  return layout;
}

} // namespace orcsdr::settings

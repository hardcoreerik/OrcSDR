#pragma once

#include <M5Unified.h>

namespace orcsdr::badge {

constexpr int kWidth = 104;
constexpr int kHeight = 104;

extern const uint8_t raw_start[] asm("_binary_orc_badge_104_rgb565_start");
extern const uint8_t raw_end[] asm("_binary_orc_badge_104_rgb565_end");

constexpr uint16_t display565(uint16_t value) {
  return static_cast<uint16_t>((value << 8) | (value >> 8));
}
static_assert(display565(0x07e0) == 0xe007);

inline bool draw(int x, int y, int size) {
  if (size <= 0 || size > kWidth || raw_end - raw_start != kWidth * kHeight * 2)
    return false;
  const auto* source = reinterpret_cast<const uint16_t*>(raw_start);
  uint16_t row[kWidth];
  for (int dy = 0; dy < size; ++dy) {
    const uint16_t* source_row = source + (dy * kHeight / size) * kWidth;
    for (int dx = 0; dx < size; ++dx)
      row[dx] = display565(source_row[dx * kWidth / size]);
    M5.Display.pushImage(x, y + dy, size, 1, row);
  }
  return true;
}

}  // namespace orcsdr::badge

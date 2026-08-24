#pragma once

#include <M5Unified.h>

namespace orcsdr::badge {

constexpr int kWidth = 104;
constexpr int kHeight = 104;

extern const uint8_t raw_start[] asm("_binary_orc_badge_104_rgb565_start");
extern const uint8_t raw_end[] asm("_binary_orc_badge_104_rgb565_end");

inline bool draw(int x, int y, int size) {
  if (size <= 0 || raw_end - raw_start != kWidth * kHeight * 2) return false;
  const float scale = static_cast<float>(size) / kWidth;
  M5.Display.pushImageRotateZoomWithAA(
      x + size * 0.5f, y + size * 0.5f, kWidth * 0.5f, kHeight * 0.5f,
      0.0f, scale, scale, kWidth, kHeight,
      reinterpret_cast<const uint16_t*>(raw_start));
  return true;
}

}  // namespace orcsdr::badge

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

constexpr int middle_center = 0;
constexpr int middle_left = 1;
constexpr uint16_t TFT_WHITE = 0xffff, TFT_BLACK = 0x0000, TFT_NAVY = 0x000f,
                   TFT_DARKGREY = 0x4208, TFT_DARKCYAN = 0x03ef,
                   TFT_DARKGREEN = 0x03e0, TFT_MAROON = 0x8000;

struct RecordedRect { int x, y, w, h; };
struct RecordedText { std::string value; int x, y, size; };
struct RecordingDisplay {
  int full_clears = 0;
  int size = 1;
  std::vector<RecordedRect> fills;
  std::vector<RecordedText> texts;
  std::string last_text;
  int width() const { return 1280; }
  int height() const { return 720; }
  void setFont(const void*) {}
  void setTextDatum(int) {}
  void setTextColor(uint16_t, uint16_t = 0) {}
  void setTextSize(int value) { size = value; }
  int32_t textWidth(const char* value) const {
    return static_cast<int32_t>(std::strlen(value)) * 6 * size;
  }
  void fillScreen(uint16_t) { ++full_clears; }
  void fillRect(int x, int y, int w, int h, uint16_t) { fills.push_back({x, y, w, h}); }
  void fillRoundRect(int x, int y, int w, int h, int, uint16_t) {
    fills.push_back({x, y, w, h});
  }
  void drawRoundRect(int, int, int, int, int, uint16_t) {}
  int32_t drawString(const char* value, int x, int y) {
    last_text = value;
    texts.push_back({value, x, y, size});
    return textWidth(value);
  }
};

struct RecordingM5 { RecordingDisplay Display; };
extern RecordingM5 M5;

inline size_t strlcpy(char* destination, const char* source, size_t capacity) {
  const size_t length = std::strlen(source);
  if (capacity) {
    const size_t count = std::min(length, capacity - 1);
    std::memcpy(destination, source, count);
    destination[count] = '\0';
  }
  return length;
}

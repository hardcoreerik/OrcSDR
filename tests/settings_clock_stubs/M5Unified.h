#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using textdatum_t = int;
inline constexpr textdatum_t middle_left = 0;
inline constexpr textdatum_t middle_center = 1;
inline constexpr textdatum_t middle_right = 2;
inline constexpr textdatum_t top_left = 3;
inline constexpr uint16_t TFT_BLACK = 0x0000, TFT_WHITE = 0xffff;
inline constexpr uint16_t TFT_DARKCYAN = 0x03ef, TFT_DARKGREEN = 0x03e0;
inline constexpr uint16_t TFT_DARKGREY = 0x4208, TFT_LIGHTGREY = 0xc618;
inline constexpr uint16_t TFT_MAROON = 0x7800, TFT_NAVY = 0x000f;
inline constexpr uint16_t TFT_ORANGE = 0xfd20, TFT_RED = 0xf800, TFT_YELLOW = 0xffe0;

extern "C" size_t strlcpy(char* destination, const char* source, size_t size);

struct RecordedFill { int x, y, w, h; uint16_t color; };
struct RecordedText { std::string text; int x, y; int size=1; };

class RecordingDisplay {
 public:
  int width() const { return width_; }
  int height() const { return height_; }
  void set_size(int w, int h) { width_ = w; height_ = h; }
  void clear_records() { fills.clear(); texts.clear(); fill_screen_calls = 0; }
  void fillScreen(uint16_t) { ++fill_screen_calls; }
  void fillRect(int x, int y, int w, int h, uint16_t color) { fills.push_back({x,y,w,h,color}); }
  void fillRoundRect(int x, int y, int w, int h, int, uint16_t color) { fillRect(x,y,w,h,color); }
  void drawRect(int, int, int, int, uint16_t) {}
  void drawRoundRect(int, int, int, int, int, uint16_t) {}
  void drawFastHLine(int, int, int, uint16_t) {}
  int drawString(const char* text, int x, int y) {
    texts.push_back({text ? text : "", x, y,text_size_});
    return textWidth(text);
  }
  int textWidth(const char* text) const { return text ? static_cast<int>(std::char_traits<char>::length(text) * 6 * text_size_) : 0; }
  void setFont(const void*) {}
  void setTextDatum(textdatum_t) {}
  void setTextSize(float value) {text_size_=static_cast<int>(value); }
  void setTextColor(uint16_t) {}
  void setTextColor(uint16_t, uint16_t) {}

  std::vector<RecordedFill> fills;
  std::vector<RecordedText> texts;
  int fill_screen_calls = 0;
 private:
  int width_ = 800, height_ = 480,text_size_=1;
};

struct M5Stub { RecordingDisplay Display; };
extern M5Stub M5;

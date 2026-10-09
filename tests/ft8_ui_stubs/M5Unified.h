#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
using textdatum_t = int;
constexpr int middle_center = 0, middle_left = 1, middle_right = 2, top_left = 3;
constexpr uint16_t TFT_WHITE = 0xffff, TFT_BLACK = 0, TFT_RED = 0xf800, TFT_DARKGREY = 0x7bef;
namespace fonts { inline int DejaVu24, DejaVu18; }
namespace lgfx {
struct Paint { std::string text; int x, y; uint16_t color; };
inline std::vector<Paint> paints;
class LovyanGFX {
  uint16_t color_ = 0;
public:
  template<class... T> void setTextDatum(T...) {}
  template<class... T> void setTextSize(T...) {}
  template<class... T> void setFont(T...) {}
  void setTextColor(uint16_t c) { color_ = c; }
  template<class... T> void fillRoundRect(T...) {}
  template<class... T> void drawRoundRect(T...) {}
  template<class... T> void fillRect(T...) {}
  template<class... T> void drawRect(T...) {}
  template<class... T> void fillScreen(T...) {}
  template<class... T> void fillCircle(T...) {}
  template<class... T> void drawCircle(T...) {}
  template<class... T> void drawFastVLine(T...) {}
  template<class... T> void drawFastHLine(T...) {}
  template<class... T> void drawLine(T...) {}
  template<class... T> void drawArc(T...) {}
  template<class... T> void fillTriangle(T...) {}
  template<class... T> void pushImage(T...) {}
  template<class... T> void setScrollRect(T...) {}
  template<class... T> void scroll(T...) {}
  template<class... T> void setClipRect(T...) {}
  void clearClipRect() {}
  void drawString(const char* s, int x, int y) { paints.push_back({s, x, y, color_}); }
  int textWidth(const char* s) { return static_cast<int>(std::strlen(s)) * 12; }
};
}
class M5Canvas : public lgfx::LovyanGFX {
public:
  explicit M5Canvas(lgfx::LovyanGFX*) {}
  template<class... T> void setPsram(T...) {}
  template<class... T> void setColorDepth(T...) {}
  template<class... T> void fillSprite(T...) {}
  template<class... T> void pushSprite(T...) {}
  void* createSprite(int, int) { return this; }
};
inline struct M5Stub { lgfx::LovyanGFX Display; } M5;
inline uint32_t millis() { return 1000; }

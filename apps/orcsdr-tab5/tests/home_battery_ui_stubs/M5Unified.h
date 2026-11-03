#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

enum textdatum_t { top_left, middle_left, middle_center, middle_right };
inline constexpr uint16_t TFT_BLACK=0, TFT_WHITE=1, TFT_LIGHTGREY=2, TFT_ORANGE=3,
    TFT_RED=4, TFT_YELLOW=5, TFT_NAVY=6, TFT_DARKCYAN=7, TFT_DARKGREEN=8, TFT_DARKGREY=9;
namespace fonts { inline int Font2; }
inline uint32_t home_battery_test_millis = 1000;
inline uint32_t millis() { return home_battery_test_millis; }

namespace lgfx { inline namespace v1 { struct rgb565_t { uint16_t value; }; } }

class M5Canvas {
 public:
  explicit M5Canvas(void*) {}
  void setPsram(bool) {}
  void setColorDepth(int) {}
  bool createSprite(int, int) { return true; }
  void fillSprite(uint16_t) {}
  template<class... T> void setTextDatum(T...) {}
  template<class... T> void setTextColor(T...) {}
  template<class... T> void setTextSize(T...) {}
  template<class... T> void setFont(T...) {}
  template<class... T> void drawString(T...) {}
  template<class... T> void drawFastHLine(T...) {}
  template<class... T> void drawFastVLine(T...) {}
  template<class... T> void drawLine(T...) {}
  template<class... T> void drawPixel(T...) {}
  template<class... T> void fillRect(T...) {}
  template<class... T> void fillRoundRect(T...) {}
  template<class... T> void drawRoundRect(T...) {}
  template<class... T> void pushSprite(T...) {}
};

class HomeBatteryDisplay {
 public:
  struct Fill { int x,y,w,h; uint16_t color; };
  int full_clears=0, start_writes=0, end_writes=0;
  std::vector<Fill> fills;
  int width() const { return 1280; }
  int height() const { return 720; }
  void reset() { full_clears=0;start_writes=end_writes=0;fills.clear(); }
  void startWrite() { ++start_writes; }
  void endWrite() { ++end_writes; }
  void fillScreen(uint16_t) { ++full_clears; }
  void fillRect(int x,int y,int w,int h,uint16_t c) { fills.push_back({x,y,w,h,c}); }
  template<class... T> void setFont(T...) {}
  template<class... T> void setTextDatum(T...) {}
  template<class... T> void setTextColor(T...) {}
  template<class... T> void setTextSize(T...) {}
  template<class... T> void drawString(T...) {}
  template<class... T> void fillRoundRect(T...) {}
  template<class... T> void drawRoundRect(T...) {}
  template<class... T> void drawRect(T...) {}
  template<class... T> void drawLine(T...) {}
  template<class... T> void drawFastHLine(T...) {}
  template<class... T> void drawFastVLine(T...) {}
  template<class... T> void drawArc(T...) {}
  template<class... T> void drawCircle(T...) {}
  template<class... T> void fillCircle(T...) {}
  template<class... T> void fillTriangle(T...) {}
  template<class... T> void setClipRect(T...) {}
  void clearClipRect() {}
  template<class... T> void setScrollRect(T...) {}
  void clearScrollRect() {}
  template<class... T> void scroll(T...) {}
  template<class... T> void pushImage(T...) {}
};
struct HomeBatteryM5 { HomeBatteryDisplay Display; };
inline HomeBatteryM5 M5;

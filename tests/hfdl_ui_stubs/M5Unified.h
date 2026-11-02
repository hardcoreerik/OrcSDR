#pragma once
// Recording substitute for M5GFX in the HFDL HOST TEST ONLY.
#include <cstdint>
#include <cstring>
constexpr uint16_t TFT_BLACK=0;
constexpr int middle_left=0;
struct HfdlFakeDisplay {
  int screen_width=1280,screen_height=720;
  uint32_t full_clears=0,region_paints=0,painted_pixels=0,strings=0;
  int width() const {return screen_width;}
  int height() const {return screen_height;}
  void fillScreen(uint16_t){++full_clears;}
  void fillRect(int,int,int w,int h,uint16_t){++region_paints;painted_pixels+=static_cast<uint32_t>(w*h);}
  void fillRoundRect(int,int,int w,int h,int,uint16_t){++region_paints;painted_pixels+=static_cast<uint32_t>(w*h);}
  void drawRoundRect(int,int,int,int,int,uint16_t){}
  void setTextDatum(int){}
  void setTextColor(uint16_t,uint16_t){}
  void setTextSize(int){}
  void drawString(const char*,int,int){++strings;}
  void pushImage(int,int,int,int,const uint16_t*){}
};
struct HfdlFakeM5{HfdlFakeDisplay Display;};
extern HfdlFakeM5 M5;

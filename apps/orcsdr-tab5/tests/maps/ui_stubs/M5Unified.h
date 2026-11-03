#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
enum textdatum_t {top_left,middle_left,middle_center,middle_right};
enum {TFT_BLACK,TFT_WHITE,TFT_NAVY,TFT_LIGHTGREY,TFT_DARKGREY,TFT_MAROON,
      TFT_DARKCYAN,TFT_DARKGREEN,TFT_ORANGE,TFT_YELLOW};
inline size_t strlcpy(char* out,const char* in,size_t capacity) {
  const auto size=std::strlen(in);
  if(capacity) {const auto copied=std::min(size,capacity-1);std::memcpy(out,in,copied);out[copied]=0;}
  return size;
}
inline uint32_t test_millis=1000;
inline uint32_t millis() {return test_millis;}
namespace lgfx {inline namespace v1 {
class LovyanGFX {
 public:
  std::vector<std::string> strings;
  std::vector<std::pair<int,int>> positions;
  int screen_clears=0,map_clears=0;
  int region_clears=0,string_draw_calls=0;
  unsigned text_size=1;
  template<class... T> void setFont(T...) {}
  template<class... T> void setTextDatum(T...) {}
  template<class T> void setTextSize(T value) {text_size=static_cast<unsigned>(value);}
  template<class... T> void setTextColor(T...) {}
  template<class... T> void fillScreen(T...) {strings.clear();positions.clear();++screen_clears;}
  void erase_region(int x,int y,int w,int h) {
    if(positions.size()!=strings.size()){positions.assign(strings.size(),{-1,-1});}
    for(size_t i=strings.size();i>0;--i) {
      const auto p=positions[i-1];
      if(p.first>=x&&p.first<x+w&&p.second>=y&&p.second<y+h) {
        strings.erase(strings.begin()+i-1);positions.erase(positions.begin()+i-1);
      }
    }
  }
  template<class C> void fillRect(int x,int y,int w,int h,C) {erase_region(x,y,w,h);++region_clears;if(x==16&&y==72&&w==1248&&h==568)++map_clears;}
  template<class... T> void fillRect(T...) {++region_clears;}
  void fillRoundRect(int x,int y,int w,int h,int,int) {erase_region(x,y,w,h);}
  template<class... T> void drawRoundRect(T...) {}
  template<class... T> void drawRect(T...) {}
  template<class... T> void drawLine(T...) {}
  template<class... T> void drawFastHLine(T...) {}
  void drawString(const char* value,int x,int y) {
    ++string_draw_calls;
    if(positions.size()!=strings.size())positions.assign(strings.size(),{-1,-1});
    strings.emplace_back(value);positions.emplace_back(x,y);
  }
};
}}
struct FakeM5 {lgfx::LovyanGFX Display;};
inline FakeM5 M5;

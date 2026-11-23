#pragma once
// TEST ONLY: records drawing operations; NEVER a pixel renderer or screenshot source.
#include <cstdint>
#include <string>
#include <vector>
constexpr uint16_t TFT_BLACK=0x0000, TFT_WHITE=0xffff;
constexpr int middle_left=0;
namespace host_stub {
struct Prim {enum Kind { fill, round_fill, round_line, text, line } kind; int x,y,w,h; std::string str;};
inline std::vector<Prim> calls;
inline int full_clear_count=0;
inline bool out_of_bounds=false;
inline int clipped_calls=0;
inline void reset(){calls.clear();full_clear_count=0;out_of_bounds=false;clipped_calls=0;}
inline void record(Prim::Kind kind,int x,int y,int w,int h,const char* s=nullptr){
  if(x<0||y<0||w<0||h<0||x+w>1280||y+h>720)out_of_bounds=true;
  calls.push_back({kind,x,y,w,h,s?s:""});
}
inline bool contains(const std::string& needle){for(auto& p:calls)if(p.kind==Prim::text&&p.str.find(needle)!=std::string::npos)return true;return false;}
}
namespace lgfx {
class LovyanGFX {
 public:
  void fillScreen(uint16_t){++host_stub::full_clear_count;host_stub::record(host_stub::Prim::fill,0,0,1280,720);}
  void fillRect(int x,int y,int w,int h,uint16_t){host_stub::record(host_stub::Prim::fill,x,y,w,h);}
  void fillRoundRect(int x,int y,int w,int h,int,uint16_t){host_stub::record(host_stub::Prim::round_fill,x,y,w,h);}
  void drawRoundRect(int x,int y,int w,int h,int,uint16_t){host_stub::record(host_stub::Prim::round_line,x,y,w,h);}
  void drawFastHLine(int x,int y,int w,uint16_t){host_stub::record(host_stub::Prim::line,x,y,w,1);}
  void setClipRect(int x,int y,int w,int h){host_stub::record(host_stub::Prim::line,x,y,w,h); ++host_stub::clipped_calls;}
  void clearClipRect(){}
  void setFont(decltype(nullptr)){}
  void setTextSize(int){}
  void setTextDatum(int){}
  void setTextColor(uint16_t){}
  void drawString(const char* s,int x,int y){host_stub::record(host_stub::Prim::text,x,y,0,0,s);}
};
}

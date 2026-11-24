#pragma once
// TEST ONLY: records drawing operations; NEVER a pixel renderer or screenshot source.
#include <cstdint>
#include <string>
#include <vector>
constexpr uint16_t TFT_BLACK=0x0000, TFT_WHITE=0xffff;
constexpr int middle_left=0;
namespace host_stub {
struct Prim {enum Kind { fill, round_fill, round_line, text, line } kind; int x,y,w,h; std::string str; int cx=0,cy=0,cw=1280,ch=720;};   // cx..ch = clip rectangle in force when it was drawn
inline std::vector<Prim> calls;
inline int full_clear_count=0;
inline bool out_of_bounds=false;
inline int clipped_calls=0;
inline int clip_x=0,clip_y=0,clip_w=1280,clip_h=720,text_size=1;
struct R {int x,y,w,h;};
// What a primitive actually touches: its bounds intersected with the clip rectangle it was drawn under.
inline R eff(const struct Prim& p){int x0=p.x>p.cx?p.x:p.cx,y0=p.y>p.cy?p.y:p.cy;int x1=(p.x+p.w<p.cx+p.cw)?p.x+p.w:p.cx+p.cw,y1=(p.y+p.h<p.cy+p.ch)?p.y+p.h:p.cy+p.ch;if(x1<x0)x1=x0;if(y1<y0)y1=y0;return {x0,y0,x1-x0,y1-y0};}
inline void reset(){calls.clear();full_clear_count=0;out_of_bounds=false;clipped_calls=0;}
inline void begin_frame(){reset();}
inline void record(Prim::Kind kind,int x,int y,int w,int h,const char* s=nullptr){
  Prim p{kind,x,y,w,h,s?s:"",clip_x,clip_y,clip_w,clip_h};
  if(kind==Prim::text){R e=eff(p);if(e.x<0||e.y<0||e.x+e.w>1280||e.y+e.h>720)out_of_bounds=true;}   // text is clipped by its widget; judge what is left
  else if(x<0||y<0||w<0||h<0||x+w>1280||y+h>720)out_of_bounds=true;
  calls.push_back(p);
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
  void setClipRect(int x,int y,int w,int h){host_stub::record(host_stub::Prim::line,x,y,w,h); ++host_stub::clipped_calls;host_stub::clip_x=x;host_stub::clip_y=y;host_stub::clip_w=w;host_stub::clip_h=h;}
  void clearClipRect(){host_stub::clip_x=0;host_stub::clip_y=0;host_stub::clip_w=1280;host_stub::clip_h=720;}
  void setFont(decltype(nullptr)){}
  void setTextSize(int s){host_stub::text_size=s;}
  void setTextDatum(int){}
  void setTextColor(uint16_t){}
  void drawString(const char* s,int x,int y){int w=(int)std::string(s).size()*6*host_stub::text_size,h=8*host_stub::text_size;host_stub::record(host_stub::Prim::text,x,y-h/2,w,h,s);}   // middle-left datum; built-in 6x8 glyph cell
};
}

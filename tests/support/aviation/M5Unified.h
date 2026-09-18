#pragma once
// Host-only M5 drawing recorder. Used to exercise the actual shared renderer;
// this is not a substitute for Tab5 font metrics or framebuffer acceptance.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
constexpr uint16_t TFT_BLACK=0, TFT_WHITE=0xffff, TFT_LIGHTGREY=0xc618,
 TFT_DARKGREY=0x7bef,TFT_DARKGREEN=0x0320,TFT_DARKCYAN=0x03ef,TFT_NAVY=0xf,
 TFT_MAROON=0x7800,TFT_ORANGE=0xfd20;
constexpr double DEG_TO_RAD=3.14159265358979323846/180;
enum textdatum_t {middle_left,middle_center,middle_right};
namespace fonts { inline int DejaVu18=18,DejaVu24=24,DejaVu40=40; }
inline uint32_t host_millis=2000;
inline uint32_t millis(){return host_millis;}
template<class T> T constrain(T x,T a,T b){return std::clamp(x,a,b);}
#if !defined(__APPLE__)
inline size_t strlcpy(char* d,const char* s,size_t n){size_t len=std::strlen(s);if(n){size_t c=std::min(len,n-1);std::memcpy(d,s,c);d[c]=0;}return len;}
#endif
namespace lgfx { inline namespace v1 {
class LovyanGFX {
 public:
  std::string svg;
  std::vector<std::string> labels;
  textdatum_t datum=middle_left;
  int size=18;
  uint16_t color=TFT_WHITE;
  static std::string rgb(uint16_t c){char b[10];std::snprintf(b,sizeof(b),"#%02x%02x%02x",(c>>11)*255/31,((c>>5)&63)*255/63,(c&31)*255/31);return b;}
  void rect(int x,int y,int w,int h,int r,uint16_t c,bool fill){
    svg+="<rect x='"+std::to_string(x)+"' y='"+std::to_string(y)+"' width='"+std::to_string(w)+"' height='"+std::to_string(h)+"' rx='"+std::to_string(r)+"' fill='"+(fill?rgb(c):"none")+"' stroke='"+rgb(c)+"'/>\n";
  }
  void fillScreen(uint16_t c){svg.clear();labels.clear();rect(0,0,1280,720,0,c,true);}
  void fillRect(int x,int y,int w,int h,uint16_t c){rect(x,y,w,h,0,c,true);}
  void drawRect(int x,int y,int w,int h,uint16_t c){rect(x,y,w,h,0,c,false);}
  void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){rect(x,y,w,h,r,c,true);}
  void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c){rect(x,y,w,h,r,c,false);}
  void drawLine(int x,int y,int xx,int yy,uint16_t c){svg+="<path d='M"+std::to_string(x)+" "+std::to_string(y)+" L"+std::to_string(xx)+" "+std::to_string(yy)+"' stroke='"+rgb(c)+"'/>\n";}
  void drawFastHLine(int x,int y,int w,uint16_t c){drawLine(x,y,x+w,y,c);}
  void drawFastVLine(int x,int y,int h,uint16_t c){drawLine(x,y,x,y+h,c);}
  void circle(int x,int y,int r,uint16_t c,bool fill){svg+="<circle cx='"+std::to_string(x)+"' cy='"+std::to_string(y)+"' r='"+std::to_string(r)+"' fill='"+(fill?rgb(c):"none")+"' stroke='"+rgb(c)+"'/>\n";}
  void drawCircle(int x,int y,int r,uint16_t c){circle(x,y,r,c,false);}
  void fillCircle(int x,int y,int r,uint16_t c){circle(x,y,r,c,true);}
  void fillTriangle(int x,int y,int x2,int y2,int x3,int y3,uint16_t c){svg+="<polygon points='"+std::to_string(x)+","+std::to_string(y)+" "+std::to_string(x2)+","+std::to_string(y2)+" "+std::to_string(x3)+","+std::to_string(y3)+"' fill='"+rgb(c)+"'/>\n";}
  void setFont(const int* f){size=f?*f:12;}
  void setTextSize(int n){(void)n;}
  void setTextColor(uint16_t c){color=c;}
  void setTextDatum(textdatum_t d){datum=d;}
  void drawString(const char* s,int x,int y){
    labels.emplace_back(s);std::string escaped;
    for(const char* p=s;*p;++p){if(*p=='&')escaped+="&amp;";else if(*p=='<')escaped+="&lt;";else escaped+=*p;}
    svg+="<text x='"+std::to_string(x)+"' y='"+std::to_string(y)+"' font-family='DejaVu Sans,sans-serif' font-size='"+std::to_string(size)+"' dominant-baseline='central' text-anchor='"+(datum==middle_center?"middle":datum==middle_right?"end":"start")+"' fill='"+rgb(color)+"'>"+escaped+"</text>\n";
  }
  void pushImage(int,int,int,int,const uint16_t*){}
  void setClipRect(int,int,int,int){}
  void clearClipRect(){}
};
}}
struct HostM5 {lgfx::LovyanGFX Display; struct { int getBatteryLevel() const { return 100; } } Power;};
inline HostM5 M5;
class M5Canvas:public lgfx::LovyanGFX {
 lgfx::LovyanGFX* parent;
 bool allocated=false;
 public:
 explicit M5Canvas(lgfx::LovyanGFX* p):parent(p){}
 void* getBuffer(){return allocated?this:nullptr;}
 void deleteSprite(){allocated=false;svg.clear();}
 void* createSprite(int,int){allocated=true;return this;}
 void fillSprite(uint16_t c){rect(0,0,646,390,0,c,true);}
 void pushSprite(int x,int y){parent->svg+="<g transform='translate("+std::to_string(x)+" "+std::to_string(y)+")'>"+svg+"</g>";}
};

#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
constexpr int middle_center=0,middle_left=1;
constexpr uint16_t TFT_WHITE=0xffff,TFT_BLACK=0,TFT_NAVY=0x000f,TFT_RED=0xf800,TFT_LIGHTGREY=0xc618;
struct RecordedRect { int x,y,w,h; };
struct RecordingDisplay {
  int w=1280,h=720,size=1,full_clears=0;
  std::vector<RecordedRect> fills;
  std::string last_text;
  int reads=0,pushes=0;
  int width() const{return w;}int height() const{return h;}
  void setFont(const void*){}void setTextDatum(int){}void clearScrollRect(){}
  void setTextColor(uint16_t,uint16_t=0){}void setTextSize(int value){size=value;}
  int textWidth(const char* text) const{return int(std::strlen(text))*6*size;}
  void fillScreen(uint16_t){++full_clears;}
  void fillRect(int x,int y,int width,int height,uint16_t){fills.push_back({x,y,width,height});}
  void fillRoundRect(int x,int y,int width,int height,int,uint16_t){fills.push_back({x,y,width,height});}
  void drawRoundRect(int,int,int,int,int,uint16_t){}
  void drawArc(int,int,int,int,float,float,uint16_t){}
  void drawLine(int,int,int,int,uint16_t){}
  void drawCircle(int,int,int,uint16_t){}
  void fillCircle(int x,int y,int r,uint16_t){fills.push_back({x-r,y-r,2*r+1,2*r+1});}
  void fillTriangle(int,int,int,int,int,int,uint16_t){}
  template<class T>void pushImage(int,int,int,int,const T*){++pushes;}
  void readRect(int,int,int width,int height,uint16_t* out){++reads;std::memset(out,0,width*height*sizeof(*out));}
  int drawString(const char* text,int,int){last_text=text;return textWidth(text);}
};
struct RecordingM5 {RecordingDisplay Display;};
extern RecordingM5 M5;

#pragma once
#include <cstdint>
#include <string>
constexpr uint16_t TFT_WHITE=0xffff;
struct FakeDisplay {
  int w=1280,h=720;
  int full_clears=0,header_draws=0,partial_draws=0;
  bool saw_waiting=false,saw_no_receiver=false,saw_receiving=false;
  int width()const{return w;}
  int height()const{return h;}
  void fillRect(int x,int y,int wi,int he,uint16_t){
    if(x==0&&y==0&&wi==w&&he==h)++full_clears;
    if(x==0&&y==0&&he==96)++header_draws;
    ++partial_draws;
  }
  void fillRoundRect(int,int,int,int,int,uint16_t){++partial_draws;}
  void drawRoundRect(int,int,int,int,int,uint16_t){++partial_draws;}
  void setTextSize(int){}
  void setTextColor(uint16_t,uint16_t){}
  void drawString(const char* s,int,int){
    if(std::string(s).find("UTC CLOCK UNVERIFIED")!=std::string::npos)saw_waiting=true;
    if(std::string(s).find("NO RTL-SDR")!=std::string::npos)saw_no_receiver=true;
    if(std::string(s).find("RECEIVING")!=std::string::npos)saw_receiving=true;
  }
};
struct FakeM5 { FakeDisplay Display; };
extern FakeM5 M5;

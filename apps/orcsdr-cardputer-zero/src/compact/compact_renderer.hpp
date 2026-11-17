#pragma once
#include "control_model.hpp"
#include "waterfall_style.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace orc::zero {
struct PixelRect { int x,y,w,h; };
struct CompactRenderer {
  static constexpr int width=320,height=170;
  static constexpr PixelRect header{0,0,320,16},frequency{0,16,320,38},status{0,54,320,18},
    spectrum{2,72,316,39},waterfall{2,111,316,41},footer{0,152,320,18};
  static constexpr uint16_t bg=0x0000,panel=0x0841,cyan=0x2e7f,green=0x6fe8,
    yellow=0xff24,muted=0x8c71,grid=0x2945,white=0xffff,red=0xf800;
  std::array<uint16_t,width*height> pixels{};
  void fill(uint16_t color){pixels.fill(color);}
  void point(int x,int y,uint16_t color){if(x>=0&&x<width&&y>=0&&y<height)pixels[size_t(y)*width+x]=color;}
  void rect(int x,int y,int w,int h,uint16_t c){if(w<=0||h<=0)return;for(int yy=std::max(0,y);yy<std::min(height,y+h);++yy)for(int xx=std::max(0,x);xx<std::min(width,x+w);++xx)point(xx,yy,c);}
  void border(int x,int y,int w,int h,uint16_t c){rect(x,y,w,1,c);rect(x,y+h-1,w,1,c);rect(x,y,1,h,c);rect(x+w-1,y,1,h,c);}
  void line(int x0,int y0,int x1,int y1,uint16_t c){int dx=std::abs(x1-x0),dy=-std::abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx+dy;for(;;){point(x0,y0,c);if(x0==x1&&y0==y1)break;int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}}
  // Compact 5x7 monospace uppercase glyphs; expanded to readable 10/14/28px.
  static std::array<uint8_t,7> glyph(char ch){
    switch(ch) {
      case 'A':return{14,17,17,31,17,17,17};case 'B':return{30,17,17,30,17,17,30};
      case 'C':return{15,16,16,16,16,16,15};case 'D':return{30,17,17,17,17,17,30};
      case 'E':return{31,16,16,30,16,16,31};case 'F':return{31,16,16,30,16,16,16};
      case 'G':return{15,16,16,23,17,17,15};case 'H':return{17,17,17,31,17,17,17};
      case 'I':return{31,4,4,4,4,4,31};case 'J':return{7,2,2,2,18,18,12};
      case 'K':return{17,18,20,24,20,18,17};case 'L':return{16,16,16,16,16,16,31};
      case 'M':return{17,27,21,21,17,17,17};case 'N':return{17,25,21,19,17,17,17};
      case 'O':return{14,17,17,17,17,17,14};case 'P':return{30,17,17,30,16,16,16};
      case 'Q':return{14,17,17,17,21,18,13};case 'R':return{30,17,17,30,20,18,17};
      case 'S':return{15,16,16,14,1,1,30};case 'T':return{31,4,4,4,4,4,4};
      case 'U':return{17,17,17,17,17,17,14};case 'V':return{17,17,17,17,17,10,4};
      case 'W':return{17,17,17,21,21,21,10};case 'X':return{17,17,10,4,10,17,17};
      case 'Y':return{17,17,10,4,4,4,4};case 'Z':return{31,1,2,4,8,16,31};
      case '0':return{14,17,19,21,25,17,14};case '1':return{4,12,4,4,4,4,14};
      case '2':return{14,17,1,2,4,8,31};case '3':return{30,1,1,14,1,1,30};
      case '4':return{2,6,10,18,31,2,2};case '5':return{31,16,16,30,1,1,30};
      case '6':return{14,16,16,30,17,17,14};case '7':return{31,1,2,4,8,8,8};
      case '8':return{14,17,17,14,17,17,14};case '9':return{14,17,17,15,1,1,14};
      case '.':return{0,0,0,0,0,12,12};case ':':return{0,12,12,0,12,12,0};
      case '-':return{0,0,0,31,0,0,0};case '+':return{0,4,4,31,4,4,0};
      case '/':return{1,1,2,4,8,16,16};case '%':return{25,25,2,4,8,19,19};
      case '_':return{0,0,0,0,0,0,31};case '(':return{2,4,8,8,8,4,2};
      case ')':return{8,4,2,2,2,4,8};case '?':return{14,17,1,2,4,0,4};
      case '=':return{0,0,31,0,31,0,0};case '>':return{16,8,4,2,4,8,16};
      case '<':return{1,2,4,8,4,2,1};case ' ':return{};
      default:return{31,17,1,6,4,0,4};
    }
  }
  void text(int x,int y,const std::string& s,uint16_t color=white,int scale=1,int limit=-1){
    const int maxchars=limit<0?int(s.size()):std::min<int>(limit,s.size());
    for(int n=0;n<maxchars;++n){auto rows=glyph(char(std::toupper(static_cast<unsigned char>(s[n]))));for(int ry=0;ry<7;++ry)for(int rx=0;rx<5;++rx)if(rows[ry]&(1<<(4-rx)))rect(x+n*6*scale+rx*scale,y+ry*scale,scale,scale,color);}
  }
  void centered(int y,const std::string& s,uint16_t c=white,int scale=1){text((width-int(s.size())*6*scale)/2,y,s,c,scale);}
  void pill(int x,int y,int w,const std::string& s,bool selected){rect(x,y,w,15,selected?0x1264:panel);border(x,y,w,15,selected?green:grid);text(x+5,y+4,s,selected?green:white,1);}
  static std::string format_mhz(uint32_t hz){char b[24];std::snprintf(b,sizeof(b),"%03u.%03u",hz/1000000,(hz%1000000)/1000);return b;}
  void render(const Snapshot& s) {
    fill(bg);
    rect(0,0,320,16,panel);text(4,4,"OrcSDR",green,1);
    text(64,4,"FM BROADCAST",cyan,1);
    const std::string conn=s.simulated?"DEMO":s.connection==Connection::ready?"RTL ON":s.connection==Connection::connecting?"OPEN...":"NO RX";
    text(320-int(conn.size())*6-5,4,conn,s.connection==Connection::ready?green:yellow,1);
    const bool pending=s.requested_hz!=s.applied_hz && s.connection==Connection::ready;
    text(12,21,format_mhz(pending?s.requested_hz:(s.applied_hz?s.applied_hz:s.requested_hz)),pending?yellow:white,3);
    text(165,26,"MHZ",cyan,2);text(223,25,"WFM",green,2);
    text(223,44,pending?"TUNING":s.simulated?"SIMULATED":"MONO",pending?yellow:muted,1);
    rect(0,54,320,18,panel);
    char buf[64];std::snprintf(buf,sizeof(buf),"STEP %uK",s.step_hz/1000);text(4,59,buf,cyan,1);
    std::snprintf(buf,sizeof(buf),"VOL %d%%",s.volume);text(104,59,s.muted?"MUTED":buf,s.muted?yellow:green,1);
    text(200,59,s.gain_auto?"GAIN AUTO":"GAIN MAN",white,1);
    // Visuals are NEVER synthesized in live mode. DEMO is explicit.
    if(s.live_spectrum||s.simulated){
      rect(2,72,316,39,0x0021);
      for(int j=0;j<4;++j)line(2,72+j*10,317,72+j*10,grid);
      for(int j=0;j<5;++j)line(2+j*79,72,2+j*79,110,grid);
      // 256 source bins are mapped with the same x coordinate in spectrum and waterfall.
      int prevx=2,prevy=110;
      for(int x=0;x<316;++x){int bin=x*256/316;int amp=s.spectrum[bin];int y=109-amp*35/255;
        if(x)line(prevx,prevy,2+x,y,green);prevx=x+2;prevy=y;}
      line(160,72,160,151,yellow);
      // 36 bounded real historical rows, newest at top.
      for(int yy=0;yy<36;++yy){int sy=111+yy;if(s.waterfall_rows<=yy)break;
        for(int x=0;x<316;++x){int bin=x*256/316;
          uint8_t a=s.waterfall[size_t(yy)][size_t(bin)];
          point(x+2,sy,orcsdr::waterfall_style::color565(orcsdr::waterfall_style::Screen::fm,float(a)/255));}}
    } else {
      rect(2,72,316,80,panel);border(2,72,316,80,grid);
      centered(92,s.connection==Connection::ready?"AWAITING IQ":"NO RECEIVER",yellow,2);
      centered(120,s.connection==Connection::ready?"SPECTRUM IDLE":"R: RECONNECT",muted,1);
    }
    rect(0,152,320,18,panel);
    text(5,158,"Z/C TUNE   F FREQ   M MUTE   TAB MENU",cyan,1);
    if(s.page!=Page::receive)overlay(s);
    else if(!s.error.empty()){rect(5,127,310,20,0x3000);text(10,133,s.error.substr(0,45),yellow,1);}
  }
  void overlay(const Snapshot& s) {
    rect(27,30,266,113,panel);border(27,30,266,113,cyan);
    switch(s.page){
      case Page::entry:
        text(40,41,"ENTER FREQUENCY",cyan,2);
        rect(39,69,241,40,bg);border(39,69,241,40,green);
        text(48,78,s.entry.empty()?"_":s.entry+"_",yellow,3);
        text(40,120,s.error.empty()?"MHZ  87.5 - 108   ENTER / ESC":s.error,s.error.empty()?muted:yellow,1);
        break;
      case Page::menu:{
        text(42,39,"ORCSDR MENU",cyan,2);
        const char* a[]={"FM RADIO","RF GAIN","AUDIO","RECEIVER","HELP"};
        for(int i=0;i<5;++i){const int y=58+i*16;rect(40,y,240,15,i==s.menu_selection?0x1264:panel);text(50,y+4,std::string(a[i]),i==s.menu_selection?green:white,1);}
        break;}
      case Page::gain:{
        text(43,39,"RF GAIN",cyan,2);
        text(44,72,s.gain_auto?"AUTO  [ENTER: MANUAL]":"MANUAL [ENTER: AUTO]",green,1);
        char b[48];std::snprintf(b,sizeof(b),"GAIN %+.1f DB",s.gain_tenth_db/10.f);text(44,92,b,white,2);
        text(44,122,"LEFT/RIGHT: SUPPORTED STEPS",muted,1);break;}
      case Page::audio:{
        text(43,39,"AUDIO OUTPUT",cyan,2);
        text(44,75,s.audio_ready?"PCM 48K S16 MONO":"NO AUDIO DEVICE",s.audio_ready?green:yellow,1);
        text(44,98,s.muted?"MUTED":"VOLUME "+std::to_string(s.volume)+"%",white,2);break;}
      case Page::receiver:{
        text(43,39,"RECEIVER",cyan,2);
        text(44,75,s.receiver_name.substr(0,37),white,1);
        text(44,98,"RATE "+std::to_string(s.applied_rate/1000)+" KSPS",muted,1);break;}
      case Page::help:{
        text(43,39,"KEYS",cyan,2);
        text(44,65,"FN+Z/C: TUNE     F: FREQ",white,1);
        text(44,79,"FN+S/D: VOLUME   M: MUTE",white,1);
        text(44,93,"TAB: MENU   G: GAIN T: STEP",white,1);
        text(44,107,"ESC: BACK   Q: EXIT",white,1);break;}
      case Page::error:{
        text(43,39,"RECEIVER ERROR",yellow,2);
        text(44,77,s.error.substr(0,37),white,1);
        text(44,107,"R: RETRY    ESC: BACK",green,1);break;}
      default:break;
    }
  }
  bool save_ppm(const std::string& path)const {
    std::ofstream out(path,std::ios::binary);if(!out)return false;
    out<<"P6\n320 170\n255\n";
    for(uint16_t p:pixels){unsigned char rgb[]{static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};
      out.write(reinterpret_cast<const char*>(rgb),3);}
    return bool(out);
  }
};
}
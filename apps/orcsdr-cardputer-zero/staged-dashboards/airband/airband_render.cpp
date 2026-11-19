#include "airband.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

namespace orc::zero::airband {
namespace {
constexpr uint16_t bg=0x0000,panel=0x0841,cyan=0x2e7f,green=0x6fe8,
    amber=0xff24,muted=0x8c71,grid=0x2945,white=0xffff,red=0xf800;
// 5x7 uppercase bitmap alphabet, adapted from this repository's compact renderer.
std::array<uint8_t,7> glyph(char c){
 switch(c){
 case 'A':return {14,17,17,31,17,17,17};case 'B':return{30,17,17,30,17,17,30};
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
std::string mhz(uint32_t v){char s[20];std::snprintf(s,sizeof s,"%03u.%06u",v/1000000,v%1000000);return s;}
}
Renderer::Renderer(){init_static();}
void Renderer::point(int x,int y,uint16_t color){if(x>=0&&x<width&&y>=0&&y<height)pixels_[size_t(y)*width+x]=color;}
void Renderer::rect(int x,int y,int w,int h,uint16_t color){
  if(w<=0||h<=0)return;
  for(int yy=std::max(0,y);yy<std::min(height,y+h);++yy)
    for(int xx=std::max(0,x);xx<std::min(width,x+w);++xx)point(xx,yy,color);
}
void Renderer::line(int x0,int y0,int x1,int y1,uint16_t color){
  int dx=std::abs(x1-x0),dy=-std::abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx+dy;
  for(;;){point(x0,y0,color);if(x0==x1&&y0==y1)break;
    const int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
void Renderer::text(int x,int y,const std::string& s,uint16_t color,int scale,int chars){
  const int n=std::min<int>(chars,s.size());
  for(int k=0;k<n;++k){if(x+6*k*scale+5*scale>width)break;
    auto g=glyph(char(std::toupper(static_cast<unsigned char>(s[size_t(k)]))));
    for(int gy=0;gy<7;++gy)for(int gx=0;gx<5;++gx)
      if(g[size_t(gy)]&(1<<(4-gx)))rect(x+6*k*scale+gx*scale,y+gy*scale,scale,scale,color);
  }
}
void Renderer::init_static(){
  if(initialized_)return;
  initialized_=true;pixels_.fill(bg);++full_clears_;
  rect(0,0,width,16,panel);text(4,1,"OrcSDR",green,2);text(92,4,"AIRBAND AM",cyan,1);
  rect(0,49,width,19,panel);rect(0,152,width,18,panel);
  text(4,157,"F:FREQ  ARROWS:TUNE  M:MUTE",muted,1,45);
  dirty_[0]={0,0,width,height};dirty_count_=1;
}
void Renderer::render(const Snapshot& s,const uint16_t* palette){
  init_static();dirty_count_=0;
  const bool header_changed=!have_last_ || s.demo!=last_.demo || s.connection!=last_.connection;
  const bool freq_changed=!have_last_ || s.selected_hz!=last_.selected_hz || s.tune_pending!=last_.tune_pending ||
      s.entry_active!=last_.entry_active || s.entry!=last_.entry || s.step_hz!=last_.step_hz || s.sideband!=last_.sideband || s.cb_channel!=last_.cb_channel;
  const bool state_changed=!have_last_ || s.volume!=last_.volume || s.user_muted!=last_.user_muted ||
      s.squelch_open!=last_.squelch_open || int(s.relative_db)!=int(last_.relative_db) ||
      s.audio_ready!=last_.audio_ready || s.connection!=last_.connection || s.squelch_db!=last_.squelch_db;
  const bool plot_changed=!have_last_ || palette_invalid_ || palette!=last_palette_ ||
      s.has_spectrum!=last_.has_spectrum || s.spectrum!=last_.spectrum ||
      s.waterfall_rows!=last_.waterfall_rows || s.waterfall!=last_.waterfall ||
      s.selected_hz!=last_.selected_hz || s.error!=last_.error;
  auto dirty=[&](int y,int h){dirty_[dirty_count_++]={0,y,width,h};rect(0,y,width,h,bg);};
  if(header_changed){dirty(0,16);rect(0,0,width,16,panel);
  text(4,1,"OrcSDR",green,2);text(92,4,s.cb?"CB RADIO":s.shortwave?"SHORTWAVE":s.broadcast?"AM RADIO":"AIRBAND AM",cyan);
  const std::string state=s.demo?"DEMO":s.connection==Connection::ready?"RTL ON":
                          s.connection==Connection::connecting?"WAIT RX":"NO RX";
  text(319-int(state.size())*6-4,4,state,s.demo?amber:s.connection==Connection::ready?green:amber);}
  if(freq_changed){dirty(16,33);
  const bool pending=s.tune_pending;
  text(5,19,s.entry_active?std::string(s.entry.data())+"_":mhz(s.selected_hz),pending?amber:white,3,12);
  text(200,23,"MHZ",cyan,2);
  text(251,18,s.sideband==1?"USB":s.sideband==2?"LSB":"AM",green,2);text(249,37,pending?"TUNING":s.entry_active?"ENTER":s.cb?"CH "+std::to_string(s.cb_channel):std::to_string(s.step_hz/1000)+" KHZ",pending?amber:muted,1,10);}
  if(state_changed){dirty(49,19);rect(0,49,width,19,panel);
  char label[64];std::snprintf(label,sizeof label,"V%03d",s.volume);text(4,54,s.user_muted?"MUTED":label,s.user_muted?amber:green);
  text(68,54,s.squelch_db==0?"SQL BYPASS":s.squelch_open?"SQL OPEN":"SQL SHUT",s.squelch_open?green:amber);
  std::snprintf(label,sizeof label,"+%.0fDB",s.relative_db);text(148,54,label,cyan);
  text(206,54,s.audio_ready?"PCM 48K":"NO AUDIO",s.audio_ready?green:amber);
  if(s.connection!=Connection::ready&& !s.demo)text(266,54,"NO IQ",amber);}
  if(plot_changed){dirty(68,84);rect(0,68,320,84,bg);
  for(int y=70;y<=106;y+=12)line(2,y,317,y,grid);
  for(int x=2;x<=318;x+=53)line(x,70,x,110,grid);
  if(s.has_spectrum){
    int prev_y=108;
    for(int x=0;x<316;++x){const int b=x*256/316;const int y=107-int(s.spectrum[size_t(b)])*35/255;
      if(x)line(x+1,prev_y,x+2,y,green);
      prev_y=y;}
    for(int row=0;row<std::min<int>(s.waterfall_rows,32);++row){
      for(int x=0;x<316;++x){const int b=x*256/316;const auto intensity=s.waterfall[size_t(row)][size_t(b)];
        const uint16_t shade=palette?palette[intensity]:uint16_t(((intensity>>3)<<11)|((intensity>>2)<<5)|(intensity>>3));
        point(x+2,151-row,shade);
      }
    }
  }else text(87,86,s.demo?"DEMO STARTING":"NO SPECTRUM",muted,1);
  // Plot represents a truthful 640 kHz zoom around the selected carrier.
  const int marker=2+(128*316/256);
  line(marker,70,marker,151,amber);
  line(marker-2,77,marker+2,77,amber);
  line(marker-2,83,marker+2,83,amber);
  // 10 kHz passband, +/-5 kHz at a 2.5 kHz/bin plot scale.
  line(marker-2,109,marker-2,114,cyan);line(marker+2,109,marker+2,114,cyan);
  rect(3,112,91,8,panel);text(5,112,"SPAN 640 KHZ",muted);
  if(s.error[0]){rect(1,121,316,11,panel);text(4,123,s.error.data(),red,1,50);}}
  last_=s;last_palette_=palette;have_last_=true;palette_invalid_=false;
}
bool Renderer::save_ppm(const std::string& path)const{
  std::ofstream out(path,std::ios::binary);if(!out)return false;
  out<<"P6\n320 170\n255\n";
  for(uint16_t p:pixels_){const unsigned char rgb[]={
    static_cast<unsigned char>(((p>>11)&31)*255/31),
    static_cast<unsigned char>(((p>>5)&63)*255/63),
    static_cast<unsigned char>((p&31)*255/31)};
    out.write(reinterpret_cast<const char*>(rgb),3);
  }return bool(out);
}
bool keyboard_action(Controller& c,char k){
  switch(k){
    case 'f':case 'F':return c.action(Action::frequency_entry);
    case '.':return c.action(Action::entry_point);
    case '\r':case '\n':return c.action(Action::entry_commit);
    case '\b':return c.action(Action::entry_backspace);
    case 27:return c.snapshot().entry_active?c.action(Action::entry_cancel):false;
    case 'm':case 'M':return c.action(Action::mute);
    case 's':case 'S':return c.action(Action::volume_down);
    case 'd':case 'D':return c.action(Action::volume_up);
    case 't':case 'T':return true; // 25k is the ONLY step; host owns stepping mode.
    case 'g':case 'G':return c.action(Action::gain_auto_toggle);
    case '[':return c.action(Action::squelch_down);
    case ']':return c.action(Action::squelch_up);
    default:
      if(k>='0'&&k<='9')return c.action(Action::entry_digit,k);
      return false; // Space, P, Tab and Q remain global.
  }
}
}

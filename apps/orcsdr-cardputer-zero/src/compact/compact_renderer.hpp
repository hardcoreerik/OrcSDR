#pragma once
#include "control_model.hpp"
#include "station_directory.hpp"
#include "orc_badge.hpp"
#include "orc_mascot.hpp"
#include "waterfall_style.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace orc::zero {
struct PixelRect { int x,y,w,h; };
struct CompactRenderer {
  static constexpr int width=320,height=170;
  static constexpr PixelRect header{0,0,320,10},frequency{0,10,320,20},status{0,30,320,10},
    spectrum{0,40,320,48},waterfall{0,88,320,72},footer{0,160,320,10};
  static constexpr uint16_t bg=0x0000,panel=0x0841,cyan=0x2e7f,green=0x6fe8,
    yellow=0xff24,muted=0x8c71,grid=0x2945,white=0xffff,red=0xf800;
  std::array<uint16_t,width*height> pixels{};
  unsigned full_clears=0;
  bool initialized=false;
  Page previous_page=Page::receive;
  void fill(uint16_t color){pixels.fill(color);++full_clears;}
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
  // Key hints for the footer: each key cap is drawn in `key_color`, its action
  // in `action_color`, so "press this, it does that" is readable at a glance.
  // Groups sit two character cells apart; returns the x just past the last one.
  int keys(int x,int y,std::initializer_list<std::pair<const char*,const char*>> items,
           int scale=1,uint16_t key_color=yellow,uint16_t action_color=muted){
    const int cell=6*scale;
    int cx=x;
    bool first=true;
    for(const auto& item:items){
      if(!first)cx+=2*cell;
      first=false;
      text(cx,y,item.first,key_color,scale);
      cx+=int(std::strlen(item.first))*cell;
      if(item.second&&*item.second){
        cx+=cell;
        text(cx,y,item.second,action_color,scale);
        cx+=int(std::strlen(item.second))*cell;
      }
    }
    return cx;
  }
  void badge(int x,int y,int size){
    const auto* icon=size==28?kOrcMascot28:size==11?kOrcMascot11:kOrcMascot21;
    const int side=size==28?28:size==11?11:21;
    for(int yy=0;yy<side;++yy)for(int xx=0;xx<side;++xx)point(x+xx,y+yy,icon[yy*side+xx]);
  }
  void centered(int y,const std::string& s,uint16_t c=white,int scale=1){text((width-int(s.size())*6*scale)/2,y,s,c,scale);}
  void pill(int x,int y,int w,const std::string& s,bool selected){rect(x,y,w,15,selected?0x1264:panel);border(x,y,w,15,selected?green:grid);text(x+5,y+4,s,selected?green:white,1);}
  static std::string format_mhz(uint32_t hz){char b[24];std::snprintf(b,sizeof(b),"%03u.%03u",hz/1000000,(hz%1000000)/1000);return b;}
  void render(const Snapshot& s) {
    const bool layout_changed=!initialized||previous_page!=s.page;
    if(layout_changed){fill(bg);initialized=true;previous_page=s.page;
      rect(footer.x,footer.y,footer.w,footer.h,panel);
      keys(4,162,{{"SPACE","DASH"},{"V","FREQ"},{"S/D","VOL"},{"P","COLOR"},{"TAB","MENU"}});
    }
    // Clear only the widgets being updated; framebuffer output skips identical pixels.
    rect(0,0,320,40,panel);
    badge(1,1,28);text(33,1,"OrcSDR",green);text(79,1,"FM BROADCAST",cyan);
    const std::string conn=s.simulated?"DEMO":s.connection==Connection::ready?(s.audio_ready?"RTL ON":"NO AUDIO"):s.connection==Connection::connecting?"OPEN...":"NO RX";
    text(316-int(conn.size())*6,1,conn,s.connection==Connection::ready?green:yellow);
    const bool pending=s.requested_hz!=s.applied_hz&&s.connection==Connection::ready;
    const uint32_t station=s.applied_hz?s.applied_hz:s.requested_hz;
    text(33,13,format_mhz(pending?s.requested_hz:station),pending?yellow:white,2);
    text(123,19,"MHZ WFM",cyan);

    const uint32_t span=s.applied_rate?s.applied_rate:kDefaultSpanHz;
    char buf[64];std::snprintf(buf,sizeof(buf),"SPAN %.3fM",span/1000000.);text(202,19,buf,cyan);
    std::snprintf(buf,sizeof(buf),"STEP %uK",s.step_hz/1000);text(4,32,buf,cyan);
    std::snprintf(buf,sizeof(buf),"VOL %d%%",s.volume);text(82,32,s.muted?"MUTED":buf,s.muted?yellow:green);
    std::snprintf(buf,sizeof(buf),"G %+.1f",s.gain_tenth_db/10.);text(145,32,s.gain_auto?"G AUTO":buf,white);
    text(208,32,orcsdr::waterfall_style::palette_name(s.palette),muted,1,18);
    // Visuals are NEVER synthesized in live mode. DEMO is explicit.
    rect(spectrum.x,spectrum.y,spectrum.w,spectrum.h,0x0021);
    rect(waterfall.x,waterfall.y,waterfall.w,waterfall.h,bg);
    if(s.live_spectrum||s.simulated){
      const uint32_t center=station;
      for(int j=0;j<4;++j)line(0,51+j*12,319,51+j*12,grid);
      for(int j=0;j<5;++j)line(j*319/4,50,j*319/4,87,grid);
      int prevy=87;
      for(int x=0;x<320;++x){const int bin=x*256/320,amp=s.spectrum[bin];
        const int y=87-amp*35/255;
        if(x)line(x-1,prevy,x,y,orcsdr::waterfall_style::color565(uint8_t(s.palette),0.55f+amp/566.f));
        prevy=y;
      }
      const auto rf_x=[&](int64_t hz){return int((hz-int64_t(center)+span/2)*320/span);};
      // Use the selected demodulator channel width around the tuned center.
      const int tuned_x=rf_x(station);
      for(int edge:{rf_x(int64_t(station)-s.bandwidth_hz/2),rf_x(int64_t(station)+s.bandwidth_hz/2)})
        if(edge>=0&&edge<320)line(edge,50,edge,87,cyan);
      for(int yy=0;yy<std::min(kWaterfallRows,s.waterfall_rows);++yy)
        for(int x=0;x<320;++x)
          point(x,waterfall.y+yy,orcsdr::waterfall_style::color565(uint8_t(s.palette),s.waterfall[size_t(yy)][size_t(x*256/320)]/255.f));
      line(tuned_x,50,tuned_x,159,yellow);
      text(2,42,format_mhz(center-span/2),muted);
      text(139,42,format_mhz(center),muted);
      text(276,42,format_mhz(center+span/2),muted);
    }else{
      centered(72,s.connection==Connection::ready?"AWAITING IQ":"NO RECEIVER",yellow,2);
      centered(109,s.connection==Connection::ready?"SPECTRUM IDLE":"R: RECONNECT",muted);
    }
    if(s.page==Page::receive&&s.fm_page)draw_fm_page(s);
    if(s.page!=Page::receive)overlay(s);
    else if(!s.error.empty()){rect(5,127,310,20,0x3000);text(10,133,s.error.substr(0,45),yellow);}
  }
  // ---- FM dashboard pages ------------------------------------------------
  // Loaded on first use and kept: the index is 1.19 MB, which is cheap to hold
  // but not cheap to parse on every repaint.
  const orc::zero::stations::Directory& station_db() const {
    static orc::zero::stations::Directory db;
    static bool tried=false;
    if(!tried){
      tried=true;
      std::string error;
      const char* candidates[]={"fm_am_stations.idx","apps/orcsdr-cardputer-zero/fm_am_stations.idx",
                                "data/fm_am_stations.idx"};
      for(const char* path:candidates)if(db.load(path,error))break;
    }
    return db;
  }
  static double station_km(const orc::zero::stations::Record& r,double lat,double lon){
    const double dx=(r.longitude()-lon)*111.32*std::cos(lat*3.14159265358979323846/180.0);
    const double dy=(r.latitude()-lat)*111.32;
    return std::sqrt(dx*dx+dy*dy);
  }
  void draw_fm_page(const Snapshot& s){
    char b[64];
    const auto& city=kFmCities[std::min<size_t>(s.fm_city,kFmCityCount-1)];
    const double lat=city.latitude_e7/1e7, lon=city.longitude_e7/1e7;
    rect(0,40,320,120,bg);
    rect(0,160,320,10,panel);  // keys() draws over the old footer otherwise
    if(s.fm_page==1){
      text(4,42,"STATIONS NEAR",muted,1,13);text(86,42,city.label,cyan,1,14);
      const auto& db=station_db();
      if(db.empty()){
        text(4,60,"NO STATION INDEX",yellow);
        text(4,74,"RUN TOOLS/BUILD_STATION_INDEX.PY",muted,1,33);
      }else{
        const auto found=db.nearest(lat,lon,orc::zero::stations::kServiceFm,7);
        for(size_t n=0;n<found.size();++n){
          const auto& r=db.at(found[n]);const int y=58+int(n)*13;
          text(4,y,r.call,white,1,7);
          std::snprintf(b,sizeof(b),"%u.%u",r.frequency_hz/1000000u,(r.frequency_hz/100000u)%10u);
          text(52,y,b,green,1,5);
          text(96,y,r.city,muted,1,10);
          std::snprintf(b,sizeof(b),"%dKM",int(station_km(r,lat,lon)+0.5));
          text(286,y,b,cyan,1,5);
        }
      }
      keys(4,160,{{"Z/C","PAGE"},{"F/X","TUNE"},{"TAB","MENU"}});
    }else if(s.fm_page==2){
      text(4,42,"RF HEALTH",muted);
      int peak=0;unsigned total=0;
      for(int n=0;n<256;++n){const int v=s.spectrum[size_t(n)];peak=std::max(peak,v);total+=unsigned(v);}
      std::snprintf(b,sizeof(b),"PEAK %d%%  AVG %d%%",peak*100/255,int(total/256)*100/255);
      text(4,56,b,peak>200?yellow:white);
      std::snprintf(b,sizeof(b),"GAIN %s",s.gain_auto?"AUTO":"");
      if(!s.gain_auto)std::snprintf(b,sizeof(b),"GAIN %+.1f DB",s.gain_tenth_db/10.);
      text(4,70,b,white);
      std::snprintf(b,sizeof(b),"VOLUME %d%%%s",s.volume,s.muted?" MUTED":"");
      text(4,82,b,white);
      std::snprintf(b,sizeof(b),"SPAN %.3f MHZ",(s.applied_rate?s.applied_rate:kDefaultSpanHz)/1e6);
      text(4,94,b,white);
      std::snprintf(b,sizeof(b),"RATE %u KSPS",(s.applied_rate?s.applied_rate:0)/1000u);
      text(4,106,b,white);
      std::snprintf(b,sizeof(b),"DROPPED %llu",(unsigned long long)s.known_dropped_samples);
      text(4,118,b,(s.known_dropped_samples?yellow:muted));
      std::snprintf(b,sizeof(b),"STEP %u KHZ",s.step_hz/1000u);
      text(4,130,b,white);
      keys(4,160,{{"Z/C","PAGE"},{"F/X","TUNE"},{"TAB","MENU"}});
    }else{
      text(4,42,"SETTINGS",muted);
      text(4,56,"LOCATION",muted,1,8);text(70,56,city.label,yellow,1,14);
      std::snprintf(b,sizeof(b),"STEP %u KHZ",s.step_hz/1000u);text(4,72,b,white);
      std::snprintf(b,sizeof(b),"VOLUME %d%%",s.volume);text(150,72,b,white);
      if(s.gain_auto)text(4,84,"GAIN AUTO",white);
      else{std::snprintf(b,sizeof(b),"GAIN %+.1f DB",s.gain_tenth_db/10.);text(4,84,b,white);}
      text(150,84,orcsdr::waterfall_style::palette_name(s.palette),white,1,12);
      std::snprintf(b,sizeof(b),"SPAN %.3f MHZ",(s.applied_rate?s.applied_rate:kDefaultSpanHz)/1e6);
      text(4,96,b,white);
      text(4,120,"F/X CHANGES LOCATION",muted,1,20);
      text(4,132,"G GAIN   T STEP   S/D VOLUME",muted,1,26);
      keys(4,160,{{"Z/C","PAGE"},{"F/X","CITY"},{"TAB","MENU"}});
    }
  }
  void overlay(const Snapshot& s) {
    rect(27,30,266,113,panel);border(27,30,266,113,cyan);
    switch(s.page){
      case Page::dashboards:{
        text(43,39,"OrcSDR",green,2);
        const int first=std::max(0,s.dashboard_selection-4);
        for(int i=0;i<5;++i){
          const char* names[]={"FM BROADCAST","ADS-B","ACARS","AIRBAND AM","SETTINGS","RF LAB","2.4 GHZ WIFI","WEATHER","MARINE","POCSAG","SSTV","AM RADIO","DEAD AIR","SHORTWAVE","CB RADIO","FT8 RX","P25 RADIO","LORA / MESH","SATELLITE"};
          const int y=65+i*13;
          rect(40,y,168,12,s.dashboard_selection==i+first?0x1264:panel);
          text(46,y+2,names[i+first],s.dashboard_selection==i+first?green:white);
        }
        for(int y=0;y<kOrcBadgeSize;++y)
          for(int x=0;x<kOrcBadgeSize;++x)
            point(220+x,65+y,kOrcBadge[y*kOrcBadgeSize+x]);
        text(44,131,"UP/DOWN  ENTER: OPEN  ESC: BACK",cyan);break;}
      case Page::settings:
        text(43,39,"OrcSDR SETTINGS",green,2);

        text(43,61,s.muted?"MUTED  M":"VOL "+std::to_string(s.volume)+"%  S/D  M MUTE",white);
        text(43,74,"FM STEP "+std::to_string(s.step_hz/1000)+"K  T",white);
        text(43,87,std::string("PALETTE ")+orcsdr::waterfall_style::palette_name(s.palette)+"  P",white,1,38);
        text(43,100,std::string("B BIAS TEE ")+(s.bias_tee?"ON":"OFF")+
          (s.bias_tee_state==-2?" DRIVER ERROR":s.bias_tee_state<0?" WAIT RX":s.bias_tee_state==int(s.bias_tee)?" APPLIED":" PENDING"),s.bias_tee?yellow:white);
        text(43,113,"ANTENNA DC POWER - COMPATIBLE RX ONLY",muted,1,39);
        text(43,130,"ESC BACK  SPACE DASHBOARDS",cyan);break;
      case Page::entry:
        text(40,41,"ENTER FREQUENCY",cyan,2);
        rect(39,69,241,40,bg);border(39,69,241,40,green);
        text(48,78,s.entry.empty()?"_":s.entry+"_",yellow,3);
        text(40,120,s.error.empty()?"MHZ  87.5 - 108   ENTER / ESC":s.error,s.error.empty()?muted:yellow,1);
        break;
      case Page::menu:{
        text(42,39,"ORCSDR MENU",cyan,2);
        const char* a[]={"FM RADIO","RF GAIN","AUDIO","RECEIVER","HELP"};
        for(int i=0;i<5;++i){const int y=58+i*16;rect(40,y,240,15,i==s.menu_selection?0x1264:panel);text(50,y,std::string(a[i]),i==s.menu_selection?green:white,2);}
        break;}
      case Page::gain:{
        text(43,39,"RF GAIN",cyan,2);
        text(44,72,s.gain_auto?"AUTO  [ENTER: MANUAL]":"MANUAL [ENTER: AUTO]",green,1);
        char b[48];std::snprintf(b,sizeof(b),"GAIN %+.1f DB",s.gain_tenth_db/10.f);text(44,92,b,white,2);
        text(44,122,"LEFT/RIGHT: SUPPORTED STEPS",muted,1);break;}
      case Page::audio:{
        text(43,39,"AUDIO OUTPUT",cyan,2);
        text(44,73,s.audio_ready?"PCM 48K S16 MONO":"NO AUDIO DEVICE",s.audio_ready?green:yellow,2);
        text(44,98,s.muted?"MUTED":"VOLUME "+std::to_string(s.volume)+"%",white,2);break;}
      case Page::receiver:{
        text(43,39,"RECEIVER",cyan,2);
        text(44,75,s.receiver_name.substr(0,37),white,1);
        text(44,98,"RATE "+std::to_string(s.applied_rate/1000)+" KSPS",muted,1);break;}
      case Page::help:{
        text(43,39,"KEYS",cyan,2);
        text(44,65,"FN+Z/C: TUNE     F: FREQ",white,1);
        text(44,79,"FN+S/D: VOLUME   M: MUTE",white,1);
        text(44,93,"TAB MENU  P COLOR G GAIN T STEP",white,1);
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

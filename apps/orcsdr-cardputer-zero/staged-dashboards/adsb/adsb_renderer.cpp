#include "adsb_renderer.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace orc::zero::adsb {
namespace {
constexpr uint16_t bg=0x0000,panel=0x0841,green=0x6fe8,cyan=0x2e7f,
                   amber=0xff24,white=0xffff,gray=0x8410,select=0x1264;
constexpr int row_y=49,row_h=17;
const char* state_text(Status s) {
  switch(s) {
    case Status::connecting:return "CONNECT";case Status::no_receiver:return "NO RX";
    case Status::waiting:return "WAITING";case Status::receiving:return "LIVE";
    case Status::stale:return "STALE";case Status::disconnected:return "LOST RX";
    case Status::error:return "ERROR";case Status::demo:return "DEMO";
  }return "ERROR";
}
uint16_t state_color(Status s) {return s==Status::receiving?green:(s==Status::demo?cyan:amber);}
// Compact 5x7 bitmap glyphs (generic renderer primitives, no decoder duplication).
std::array<uint8_t,7> glyph(char letter) {
  switch(static_cast<char>(std::toupper(static_cast<unsigned char>(letter)))) {
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
    case '_':return{0,0,0,0,0,0,31};case '>':return{16,8,4,2,4,8,16};
    case ' ':return{};default:return{14,17,1,2,4,0,4};
  }
}
uint64_t age_sec(const Snapshot& s,const Aircraft& a) {
  return s.now_ns>=a.last_seen_ns?(s.now_ns-a.last_seen_ns)/1000000000ull:0;
}
void age_label(const Snapshot& s,const Aircraft& a,char (&buffer)[12]) {
  const auto n=age_sec(s,a);
  if(n<60)std::snprintf(buffer,sizeof(buffer),"%lluS",static_cast<unsigned long long>(n));
  else std::snprintf(buffer,sizeof(buffer),"%lluM",static_cast<unsigned long long>(std::min<uint64_t>(99,n/60)));
}
void row_label(const Snapshot& s,size_t index,char (&out)[90]) {
  if(index>=s.count) {out[0]=0;return;}
  const auto& a=s.aircraft[index];char alt[16],fresh[12];age_label(s,a,fresh);
  if(a.has_altitude)std::snprintf(alt,sizeof(alt),"%d",a.altitude_ft);
  else std::snprintf(alt,sizeof(alt),"--");
  std::snprintf(out,sizeof(out),"%06X %-8.8s %-7s %s",a.icao,a.has_callsign?a.callsign:"--",alt,fresh);
}
}
void Renderer::rect(int x,int y,int w,int h,uint16_t c) {
  const int xmin=std::max(0,x),xmax=std::min(width,x+w);
  const int ymin=std::max(0,y),ymax=std::min(height,y+h);
  if(xmin>=xmax||ymin>=ymax)return;
  for(int yy=ymin;yy<ymax;++yy) {
    std::fill_n(pixels_.begin()+yy*width+xmin,xmax-xmin,c);
  }
  stats_.pixels_painted+=static_cast<uint64_t>(xmax-xmin)*(ymax-ymin);
}
void Renderer::text(int x,int y,const char* s,uint16_t c,int max,int scale) {
  if(!s||max<1||scale<1)return;
  for(int i=0;i<max&&s[i];++i) {
    if(x+i*6*scale+5*scale>width)break;
    const auto rows=glyph(s[i]);
    for(int ry=0;ry<7;++ry)for(int rx=0;rx<5;++rx)
      if(rows[ry]&(1<<(4-rx)))rect(x+i*6*scale+rx*scale,y+ry*scale,scale,scale,c);
  }
}
void Renderer::header(const Snapshot& s) {
  ++stats_.dirty_widgets;
  rect(0,0,320,35,bg);rect(0,0,320,17,panel);
  text(4,2,"OrcSDR",green,6,2);text(93,3,"ADS-B",cyan,5,2);
  text(255,5,s.is_demo?"DEMO":state_text(s.status),state_color(s.status),10,1);
  text(4,23,"1090 MHZ",white,8);text(75,23,"2.048M CU8",cyan,12);
  char buffer[44];
  std::snprintf(buffer,sizeof(buffer),"A%zu U%llu C%llu",s.count,
    static_cast<unsigned long long>(s.counters.unique_seen),
    static_cast<unsigned long long>(s.counters.crc_ok));
  text(172,23,buffer,green,24);
}
void Renderer::footer(const Snapshot& s) {
  ++stats_.dirty_widgets;
  rect(0,152,320,18,panel);
  text(4,158,s.detail?"ESC BACK  UP/DN SELECT  SPACE APP":"UP/DN SELECT  ENTER DETAIL  SPACE APP",cyan,49);
}
void Renderer::list_row(const Snapshot& s,size_t row) {
  if(row>=kVisibleRows)return;
  ++stats_.dirty_widgets;
  const int y=row_y+static_cast<int>(row)*row_h;
  const size_t index=s.scroll+row;
  rect(0,y,width,row_h,index<s.count && index==s.selection?select:bg);
  if(index>=s.count)return;
  const auto& a=s.aircraft[index];char buf[24];
  std::snprintf(buf,sizeof(buf),"%06X",a.icao);text(6,y+5,buf,index==s.selection?green:white,6);
  text(56,y+5,a.has_callsign?a.callsign:"--",a.has_callsign?white:gray,8);
  if(a.has_altitude)std::snprintf(buf,sizeof(buf),"%d",a.altitude_ft);else std::snprintf(buf,sizeof(buf),"--");
  text(145,y+5,buf,a.has_altitude?green:gray,8);
  char fresh[12];age_label(s,a,fresh);text(273,y+5,fresh,cyan,6);
}
void Renderer::detail(const Snapshot& s) {
  ++stats_.dirty_widgets;
  rect(0,36,320,116,bg);
  if(!s.count){text(75,88,"NO AIRCRAFT",amber,20,2);return;}
  const auto& a=s.aircraft[s.selection];char b[70];
  std::snprintf(b,sizeof(b),"ICAO %06X  %-.8s",a.icao,a.has_callsign?a.callsign:"--");
  text(5,40,b,green,38,2);
  if(a.has_altitude)std::snprintf(b,sizeof(b),"ALT %d FT",a.altitude_ft);else std::snprintf(b,sizeof(b),"ALT -- FT");
  text(8,60,b,white);
  if(a.has_speed)std::snprintf(b,sizeof(b),"SPD %d KT",a.speed_kts);else std::snprintf(b,sizeof(b),"SPD -- KT");
  text(173,60,b,white);
  if(a.has_heading)std::snprintf(b,sizeof(b),"HDG %d DEG",a.heading_deg);else std::snprintf(b,sizeof(b),"HDG -- DEG");
  text(8,77,b,white);
  if(a.has_vertical_rate)std::snprintf(b,sizeof(b),"VERT %+d FPM",a.vertical_rate_fpm);else std::snprintf(b,sizeof(b),"VERT -- FPM");
  text(173,77,b,white);
  if(a.has_position)std::snprintf(b,sizeof(b),"LAT %.4f LON %.4f",a.latitude,a.longitude);
  else std::snprintf(b,sizeof(b),"LAT --  LON --");
  text(8,96,b,a.has_position?cyan:gray,47);
  if(a.has_signal)std::snprintf(b,sizeof(b),"SIGNAL %u REL",a.relative_signal);
  else std::snprintf(b,sizeof(b),"SIGNAL -- REL");
  text(8,114,b,white);
  char fresh[12];age_label(s,a,fresh);std::snprintf(b,sizeof(b),"AGE %s",fresh);
  text(198,114,b,cyan);
  text(8,134,"POSITION: GLOBAL CPR PAIR ONLY",gray,40);
}
void Renderer::main_area(const Snapshot& s) {
  if(s.detail){detail(s);return;}
  ++stats_.dirty_widgets;
  rect(0,36,320,13,panel);
  text(6,40,"ICAO",cyan);text(56,40,"CALL",cyan);text(145,40,"ALT FT",cyan);text(273,40,"AGE",cyan);
  for(size_t r=0;r<kVisibleRows;++r)list_row(s,r);
  if(!s.count) {
    text(85,86,"NO AIRCRAFT",amber,16,2);
    text(48,113,s.is_demo?"DEMO EMPTY TABLE":"WAITING FOR CRC-VALID FRAMES",gray,37);
  }
}
void Renderer::render(const Snapshot& s,unsigned palette_index) {
  (void)palette_index; // No signal plot to color; host owns palette persistence.
  if(!initialized_) {
    pixels_.fill(bg);++stats_.full_clears;initialized_=true;
    header(s);main_area(s);footer(s);
  } else {
    if(s.status!=previous_.status||s.is_demo!=previous_.is_demo||
       s.counters.crc_ok!=previous_.counters.crc_ok||
       s.counters.unique_seen!=previous_.counters.unique_seen||
       s.count!=previous_.count) header(s);
    if(s.detail!=previous_.detail||(s.count==0)!=(previous_.count==0)||
       s.scroll!=previous_.scroll||s.count!=previous_.count) main_area(s);
    else if(s.detail) {
      if(s.revision!=previous_.revision||s.now_ns/1000000000ull!=previous_.now_ns/1000000000ull)
        detail(s);
    } else {
      for(size_t r=0;r<kVisibleRows;++r) {
        char a[90],b[90];row_label(previous_,previous_.scroll+r,a);row_label(s,s.scroll+r,b);
        if(std::strcmp(a,b)!=0||s.selection==s.scroll+r||previous_.selection==previous_.scroll+r)
          if(std::strcmp(a,b)!=0||s.selection!=previous_.selection||s.revision!=previous_.revision)
            list_row(s,r);
      }
    }
    if(s.detail!=previous_.detail)footer(s);
  }
  previous_=s;
}
bool Renderer::save_ppm(const std::string& path) const {
  std::ofstream out(path,std::ios::binary|std::ios::trunc);
  if(!out)return false;
  out<<"P6\n320 170\n255\n";
  for(uint16_t p:pixels_) {
    unsigned char rgb[]{static_cast<unsigned char>(((p>>11)&31)*255/31),
                        static_cast<unsigned char>(((p>>5)&63)*255/63),
                        static_cast<unsigned char>((p&31)*255/31)};
    out.write(reinterpret_cast<const char*>(rgb),3);
  }
  return bool(out);
}
} // namespace orc::zero::adsb

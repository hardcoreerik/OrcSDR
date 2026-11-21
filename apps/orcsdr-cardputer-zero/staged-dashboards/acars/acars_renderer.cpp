// SPDX-License-Identifier: AGPL-3.0-only
#include "acars_renderer.hpp"
#include "compact_renderer.hpp" // reuse native 5x7 glyphs, not a second font
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace orc::zero::acars_dashboard {
namespace {
constexpr uint16_t bg=0x0000,panel=0x0841,green=0x6fe8,cyan=0x2e7f,
  amber=0xff24,white=0xffff,muted=0x8c71,select=0x1264,red=0xf800;
constexpr size_t kMaxLineChars=49;
constexpr int kListY=56,kRowHeight=19;
std::string mhz(uint32_t hz){char b[20];std::snprintf(b,sizeof(b),"%u.%03u",hz/1000000,(hz%1000000)/1000);return b;}
uint32_t age_seconds(uint64_t now,uint64_t then) {return uint32_t(std::min<uint64_t>(99999,now>=then?(now-then)/1000000000u:0));}
std::string safe_ascii(const char* bytes,size_t count,size_t max_chars) {
  std::string out;out.reserve(std::min(count,max_chars));
  for(size_t i=0;i<count&&i<max_chars;++i) {
    const unsigned char c=static_cast<unsigned char>(bytes[i]);
    out.push_back(c>=32&&c<=126?char(c):'.');
  }
  return out;
}
std::string safe_id(const char* bytes,size_t count) {
  bool has_value=false;
  for(size_t i=0;i<count;++i)
    if(bytes[i]!=0&&bytes[i]!=' '&&bytes[i]!='.') {has_value=true;break;}
  return has_value?safe_ascii(bytes,count,count):"-";
}
}
std::string escaped_payload(const orcsdr::acars::Message& m) {
  std::string out;
  const size_t length=std::min<size_t>(m.text_length,orcsdr::acars::kMaxBlockBytes);
  out.reserve(length*4);
  const char* hex="0123456789ABCDEF";
  for(size_t i=0;i<length;++i){const auto c=static_cast<unsigned char>(m.text[i]);
    if(c>=32&&c<=126)out.push_back(char(c));
    else {out.push_back('<');out.push_back(hex[c>>4]);out.push_back(hex[c&15]);out.push_back('>');}
  }
  return out;
}
size_t detail_line_count(const orcsdr::acars::Message& message) {
  const size_t length=escaped_payload(message).size();
  return std::max<size_t>(1,(length+kMaxLineChars-1)/kMaxLineChars);
}
void Renderer::dirty(int x,int y,int w,int h){
  x=std::max(0,x);y=std::max(0,y);w=std::min(w,width-x);h=std::min(h,height-y);
  if(w>0&&h>0&&dirty_count_<dirty_.size())dirty_[dirty_count_++]={x,y,w,h};
}
void Renderer::fill(int x,int y,int w,int h,uint16_t color) {
  const int x0=std::max(0,x),y0=std::max(0,y),x1=std::min(width,x+w),y1=std::min(height,y+h);
  for(int yy=y0;yy<y1;++yy)for(int xx=x0;xx<x1;++xx)pixels_[size_t(yy)*width+xx]=color;
}
void Renderer::glyph_text(int x,int y,const char* bytes,size_t n,uint16_t color,size_t limit) {
  for(size_t i=0;i<std::min(n,limit)&&x+int(i)*6+5<width;++i){
    const unsigned char c=static_cast<unsigned char>(bytes[i]);
    const auto glyph=orc::zero::CompactRenderer::glyph(char(std::toupper(c>=32&&c<=126?c:'?')));
    for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)
      if(glyph[yy]&(1<<(4-xx)))fill(x+int(i)*6+xx,y+yy,1,1,color);
  }
}
void Renderer::text(int x,int y,const char* str,uint16_t color,size_t limit) {
  glyph_text(x,y,str,std::strlen(str),color,limit);
}
void Renderer::header(const Snapshot& s) {
  fill(0,0,320,16,panel);
  text(33,4,"OrcSDR",green,8);
  text(95,4,"ACARS",cyan,7);

  const char* state=s.source==Source::demo?"DEMO":s.receiver==ReceiverState::receiving?"LIVE":
    s.receiver==ReceiverState::connecting?"CONNECT":s.receiver==ReceiverState::error?"ERROR":"NO RX";
  text(256,4,state,s.source==Source::demo?amber:s.receiver==ReceiverState::receiving?green:amber,10);
  dirty(0,0,320,16);
}
void Renderer::status(const Snapshot& s) {
  fill(0,16,320,40,bg);
  const uint32_t hz=s.applied_channel_hz?s.applied_channel_hz:s.requested_channel_hz;
  const auto freq=mhz(hz);
  text(33,18,freq.c_str(),white,12);text(94,18,"MHZ",cyan,3);
  text(126,18,"AM 24KS",green,8);
  char gain[28];if(s.auto_gain)std::snprintf(gain,sizeof(gain),"GAIN AUTO");
  else std::snprintf(gain,sizeof(gain),"GAIN %+.1f",double(s.gain_tenth_db)/10.);
  text(202,18,gain,cyan,18);
  char count[64];
  if(s.source==Source::demo) {
    std::snprintf(count,sizeof(count),"DEMO %zu BLOCKS  DECODER COUNTERS N/A",s.held);
    text(5,30,count,amber,52);
    text(5,41,"240K CU8  SIMULATED MESSAGE DATA",muted,52);
  }else{
    std::snprintf(count,sizeof(count),"OK %u FIX %u CRCX %u FRM %u OVF %u",s.counters.valid,
      s.counters.corrected,s.counters.crc_rejected,s.counters.framing_rejected,s.counters.overflows);
    text(5,30,count,white,52);
    std::snprintf(count,sizeof(count),"240K CU8 DROP %llu RESET %llu UILOSS %llu",(unsigned long long)s.counters.known_queue_dropped_samples,
      (unsigned long long)s.counters.stream_resets,(unsigned long long)s.counters.dropped_display_events);
    text(5,41,count,muted,52);
  }
  dirty(0,16,320,40);
}
void Renderer::rows(const Snapshot& s,uint64_t now) {
  fill(0,kListY,320,96,bg);
  if(s.held==0){
    const char* label=s.receiver==ReceiverState::receiving?"AWAITING VERIFIED ACARS":
      s.receiver==ReceiverState::connecting?"CONNECTING RECEIVER":s.receiver==ReceiverState::error?"RECEIVER ERROR":"RECEIVER DISCONNECTED";
    text(20,90,label,amber,46);
    text(20,107,"NO MESSAGES",muted,26);
  }else{
    const size_t selected_rank=s.held-1-std::min(s.selected,s.held-1);
    const size_t offset=selected_rank>=kRows?selected_rank-(kRows-1):0;
    for(size_t row=0;row<kRows&&offset+row<s.held;++row) {
      const size_t index=s.held-1-offset-row;
      const auto& rec=s.history[index];const auto& m=rec.message;
      const int y=kListY+int(row)*kRowHeight;
      if(index==s.selected)fill(0,y,320,kRowHeight,select);
      text(2,y+1,rec.unread?"N":">",rec.unread?amber:muted,1);
      const auto tail=safe_id(m.tail,7), label=safe_id(m.label,2),flight=safe_id(m.flight,6);
      text(12,y+1,tail.c_str(),white,7);
      text(62,y+1,label.c_str(),cyan,2);
      text(86,y+1,flight.c_str(),white,6);
      char age[24];if(rec.monotonic_ns)std::snprintf(age,sizeof(age),"%us",age_seconds(now,rec.monotonic_ns));
      else std::snprintf(age,sizeof(age),"AGE?");
      text(255,y+1,age,muted,7);
      if(m.corrected)text(205,y+1,"FIX",amber,3);
      else text(205,y+1,m.end_marker==0x17?"ETB":"ETX",green,3);
      const auto preview=safe_ascii(m.text,std::min<size_t>(m.text_length,orcsdr::acars::kMaxBlockBytes),48);
      text(12,y+10,preview.c_str(),muted,48);
    }
  }
  fill(0,152,320,18,panel);
  text(3,157,s.detail?"UP/DN SCROLL  ESC BACK":"UP/DN SELECT ENTER DETAIL F FREQ Z/C TUNE",cyan,52);
  dirty(0,kListY,320,114);
}
void Renderer::detail(const Snapshot& s) {
  fill(0,16,320,136,panel);
  if(!s.held){text(7,45,"NO MESSAGE",amber,22);dirty(0,16,320,136);return;}
  const auto& rec=s.history[std::min(s.selected,s.held-1)];const auto& m=rec.message;
  char b[64];
  std::snprintf(b,sizeof(b),"%s %s %s",mhz(m.frequency_hz).c_str(),
    m.corrected?"CRC CORRECTED":"CRC VERIFIED",m.end_marker==0x17?"ETB":m.end_marker==0x03?"ETX":"UNKNOWN");
  text(33,19,b,green,47);
  auto tail=safe_id(m.tail,7),flight=safe_id(m.flight,6),label=safe_id(m.label,2);
  std::snprintf(b,sizeof(b),"TAIL %s FLT %s LAB %s",tail.c_str(),flight.c_str(),label.c_str());
  text(6,31,b,white,50);
  std::snprintf(b,sizeof(b),"RAW %u TEXT %u FIXED BITS %u",unsigned(m.raw_bytes),unsigned(m.text_length),unsigned(m.corrected_bits));
  text(6,42,b,cyan,50);
  const std::string payload=escaped_payload(m);
  const size_t total=std::max<size_t>(1,(payload.size()+kMaxLineChars-1)/kMaxLineChars);
  const size_t offset=std::min(s.detail_scroll,total>11?total-11:0);
  for(size_t i=0;i<11&&offset+i<total;++i) {
    const size_t pos=(offset+i)*kMaxLineChars;
    if(pos<payload.size())glyph_text(8,55+int(i)*8,payload.data()+pos,
        std::min(kMaxLineChars,payload.size()-pos),white,kMaxLineChars);
  }
  fill(0,152,320,18,panel);
  text(4,157,"UP/DN SCROLL  ESC BACK  ETB=CONTINUED",cyan,52);
  dirty(0,16,320,154);
}
void Renderer::render(const Snapshot& s,uint64_t now_ns) {
  dirty_count_=0;
  const bool initial=!initialized_;
  if(initial) {pixels_.fill(bg);++full_clears_;initialized_=true;}
  const bool page_changed=initial||s.detail!=previous_.detail;
  if(initial||s.source!=previous_.source||s.receiver!=previous_.receiver)header(s);
  if(s.detail) {
    if(page_changed||s.revision!=previous_.revision)detail(s);
  }else{
    if(page_changed||s.applied_channel_hz!=previous_.applied_channel_hz||
       s.requested_channel_hz!=previous_.requested_channel_hz||s.auto_gain!=previous_.auto_gain||
       s.gain_tenth_db!=previous_.gain_tenth_db||
       std::memcmp(&s.counters,&previous_.counters,sizeof(Counters))!=0)status(s);
    if(page_changed||s.revision!=previous_.revision||now_ns/1000000000u!=previous_second_)
      rows(s,now_ns);
  }
  for(int y=0;y<28;++y)for(int x=0;x<28;++x)fill(1+x,1+y,1,1,orc::zero::kOrcMascot28[y*28+x]);
  previous_=s;
  previous_second_=now_ns/1000000000u;
}
bool Renderer::save_ppm(const std::string& path)const {
  std::ofstream out(path,std::ios::binary);if(!out)return false;
  out<<"P6\n320 170\n255\n";
  for(uint16_t p:pixels_){const unsigned char rgb[]{
    static_cast<unsigned char>(((p>>11)&31)*255/31),
    static_cast<unsigned char>(((p>>5)&63)*255/63),
    static_cast<unsigned char>((p&31)*255/31)};
    out.write(reinterpret_cast<const char*>(rgb),3);
  }return bool(out);
}
}

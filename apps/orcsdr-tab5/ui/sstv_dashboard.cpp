#include "sstv_dashboard.hpp"
#include <M5Unified.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace orcsdr::sstv {
namespace {
constexpr uint16_t kBg = 0x080D, kPanel = 0x1120, kBorder = 0x2B6A;
constexpr uint16_t kCyan = 0x05FF, kBlue = 0x345F, kGreen = 0x8628;
constexpr uint16_t kOrange = 0xFCA0, kWhite = 0xEF7D, kDim = 0x9CD3;
constexpr uint16_t kRed = 0xF2C8;
constexpr int kTabY=120, kTabH=55, kBodyY=190, kBodyH=458;
constexpr const char* kPages[] = {"RECEIVE", "IMAGES", "SIGNAL", "FREQUENCIES", "SETUP"};
Page g_page = Page::receive;
UiSnapshot g_ui{};
bool g_active = false;
uint8_t g_profile = 1;
size_t g_gallery_selected = 0;
char g_notice[84]{};
uint32_t g_notice_until = 0;
uint32_t g_last_render_ms = 0;
uint32_t g_last_live_serial = 0;
uint16_t g_last_row = 0;
static uint16_t g_thumb[100*76];
bool hit(int x,int y,int left,int top,int width,int height) {
  return x>=left && x<left+width && y>=top && y<top+height;
}
void txt(const char* s,int x,int y,uint16_t color=kWhite,int sz=1,
         textdatum_t datum=middle_left) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextColor(color,kBg);
  M5.Display.setFont(sz == 1 ? &fonts::DejaVu18 : &fonts::DejaVu24);
  M5.Display.setTextSize(1);
  M5.Display.drawString(s,x,y);
  M5.Display.setFont(nullptr);
}
void box(int x,int y,int w,int h,uint16_t border=kBorder, uint16_t fill=kPanel) {
  M5.Display.fillRoundRect(x,y,w,h,9,fill);
  M5.Display.drawRoundRect(x,y,w,h,9,border);
}
void label(const char* heading,const char* value,int x,int y,uint16_t accent=kWhite) {
  txt(heading,x,y,kDim);
  txt(value,x,y+31,accent,2);
}
void button(int x,int y,int w,int h,const char* name,bool on=false) {
  box(x,y,w,h,on?kCyan:kBorder,on?0x132D:kPanel);
  txt(name,x+w/2,y+h/2,on?kCyan:kWhite,1,middle_center);
}
void draw_header() {
  M5.Display.fillRect(0,0,1280,109,kBg);
  txt("SSTV",26,41,kWhite,2);
  txt("SLOW SCAN TELEVISION  /  RX ONLY",26,83,kDim);
  M5.Display.drawFastVLine(379,20,77,kBorder);
  char b[64];
  std::snprintf(b,sizeof(b),"%0.5f MHz",g_ui.frequency_hz/1e6);
  txt(b,410,38,kCyan,2);
  const auto m=metrics();
  txt(m.receiving ? "IMAGE IN PROGRESS" : (g_ui.streaming ? "LISTENING FOR VIS" : "RECEIVER IDLE"),
      411,82,m.receiving?kGreen:kDim);
  txt(g_ui.receiver_ready?"RTL-SDR CONNECTED":"NO RTL-SDR",890,37,
      g_ui.receiver_ready?kGreen:kOrange);
  txt(g_ui.sd_ready?"SD READY":"SD NOT READY",890,81,
      g_ui.sd_ready?kGreen:kOrange);
  M5.Display.drawFastHLine(16,109,1248,kCyan);
}
void draw_tabs() {
  constexpr int tabX=18, tabGap=8, tabW=242;
  for(int i=0;i<5;++i) {
    const int x=tabX+i*(tabW+tabGap);
    const bool current=static_cast<int>(g_page)==i;
    box(x,kTabY,tabW,kTabH,current?kCyan:kBorder,current?0x132D:kPanel);
    if(current) M5.Display.fillRect(x+11,kTabY+4,tabW-22,3,kCyan);
    txt(kPages[i],x+tabW/2,kTabY+29,current?kWhite:kDim,1,middle_center);
  }
}
void draw_footer() {
  M5.Display.fillRect(0,659,1280,61,kBg);
  M5.Display.drawFastHLine(16,659,1248,kBorder);
  const auto m=metrics();
  txt("ORC SDR  /  SSTV LAB",24,686,kCyan);
  txt(m.status,340,686,m.receiving?kGreen:kDim);
  txt("HOME",1214,686,kWhite,1,middle_center);
  M5.Display.drawRoundRect(1165,667,95,43,7,kBorder);
  if(g_notice[0] && millis()<g_notice_until) {
    M5.Display.fillRect(344,672,745,31,kBg);
    txt(g_notice,348,686,kOrange);
  }
}
void image_area(int x,int y,int w,int h,const Image& im,const char* empty) {
  box(x,y,w,h,kBorder,0x0840);
  // Exact decoded pixels; never synthesize an image or pretend a signal was received.
  if(!im.pixels || !im.width || !im.height) {
    M5.Display.drawRect(x+22,y+20,w-44,h-40,kBorder);
    for(int i=0;i<7;++i)
      M5.Display.drawFastHLine(x+30,y+35+i*58,w-60,0x1964);
    txt(empty,x+w/2,y+h/2,kDim,1,middle_center);
    return;
  }
  const int px=x+(w-im.width)/2, py=y+(h-im.height)/2;
  M5.Display.pushImage(px,py,im.width,im.height,im.pixels);
  M5.Display.drawRect(px-2,py-2,im.width+4,im.height+4,kCyan);
}
void draw_receive() {
  image_area(18,kBodyY,754,kBodyH,live_image(),"WAITING FOR FIRST IMAGE / VIS HEADER");
  auto m=metrics();
  box(788,kBodyY,475,kBodyH);
  txt("RECEPTION",810,217,kCyan,2);
  M5.Display.drawFastHLine(811,251,425,kBorder);
  char value[80];
  std::snprintf(value,sizeof(value),"%s",m.receiving?mode_name(m.received_mode):"AUTO DETECT");
  label("DETECTED MODE",value,812,274,m.receiving?kGreen:kWhite);
  std::snprintf(value,sizeof(value),"%u / %u",(unsigned)m.row,(unsigned)m.rows);
  label("IMAGE LINES",value,812,357);
  const int meter = m.rows ? 400*m.row/m.rows : 0;
  M5.Display.fillRoundRect(812,415,400,17,6,kBorder);
  if(meter) M5.Display.fillRoundRect(812,415,meter,17,6,kGreen);
  std::snprintf(value,sizeof(value),"%0.0f Hz   /   %0.1f%% peak",
      m.tone_hz,m.audio_peak*100.f);
  txt(value,813,465,kDim);
  std::snprintf(value,sizeof(value),"VIS OK: %lu    ERR: %lu",
      (unsigned long)m.good_vis,(unsigned long)m.bad_vis);
  txt(value,813,506,kDim);
  button(810,554,204,68,"ABORT IMAGE",false);
  button(1024,554,214,68,"SAVE LATEST",false);
}
void thumbnail(const Image& im,int x,int y,int w,int h) {
  box(x,y,w,h,kBorder,0x0840);
  if(!im.pixels) { txt("EMPTY",x+w/2,y+h/2,kDim,1,middle_center);return; }
  const int tw=100, th=76;
  for(int yy=0;yy<th;++yy) for(int xx=0;xx<tw;++xx)
    g_thumb[yy*tw+xx]=im.pixels[
      (static_cast<size_t>(yy)*im.height/th)*im.width+(xx*im.width/tw)];
  M5.Display.pushImage(x+8,y+8,tw,th,g_thumb);
}
void draw_gallery() {
  box(18,kBodyY,840,kBodyH);
  txt("RECEIVED IMAGES",42,215,kCyan,2);
  char info[50];
  std::snprintf(info,sizeof(info),"%u IN MEMORY",(unsigned)archive_count());
  txt(info,590,215,kDim);
  if(archive_count()==0) {
    txt("NO COMPLETED IMAGES YET",435,430,kDim,1,middle_center);
    txt("Completed SSTV transmissions are added here automatically",435,472,kDim,1,middle_center);
  } else {
    for(size_t i=0;i<archive_count() && i<4;++i) {
      const int xx=35+(i%2)*405, yy=250+(i/2)*176;
      thumbnail(archived_image(i),xx,yy,387,155);
      const auto im=archived_image(i);
      char line[64];
      std::snprintf(line,sizeof(line),"#%lu  %s", (unsigned long)im.serial,mode_name(im.mode));
      txt(line,xx+121,yy+42,kWhite);
      std::snprintf(line,sizeof(line),"%u x %u",im.width,im.height);
      txt(line,xx+121,yy+77,kDim);
      if(i==g_gallery_selected) M5.Display.drawRoundRect(xx+2,yy+2,383,151,9,kCyan);
    }
  }
  box(874,kBodyY,388,kBodyH);
  txt("SELECTED IMAGE",892,220,kCyan,2);
  const Image im=archived_image(g_gallery_selected);
  if(im.pixels) {
    char text_buf[80];
    std::snprintf(text_buf,sizeof(text_buf),"RECEPTION #%lu", (unsigned long)im.serial);
    txt(text_buf,894,279,kWhite);
    txt(mode_name(im.mode),894,312,kDim);
    std::snprintf(text_buf,sizeof(text_buf),"%0.5f MHz",im.frequency_hz/1e6);
    txt(text_buf,894,349,kWhite,2);
  } else txt("NOTHING SELECTED",894,312,kDim);
  button(892,449,352,65,"SAVE BMP TO SD");
  button(892,542,352,65,"CLEAR RAM GALLERY");
}
void draw_analyzer() {
  box(18,kBodyY,850,kBodyH);
  txt("AUDIO TONE MONITOR",43,220,kCyan,2);
  const int px=48,py=275,pw=782,ph=268;
  M5.Display.fillRect(px,py,pw,ph,0x0840);
  for(int i=0;i<5;++i) {
    const int yy=py+24+i*53;
    M5.Display.drawFastHLine(px,yy,pw,kBorder);
  }
  const Metrics m=metrics();
  const int center=py+ph-(std::clamp(m.tone_hz,600.f,2500.f)-600.f)*ph/1900.f;
  M5.Display.drawFastHLine(px+4,center,pw-8,m.signal?kCyan:kDim);
  char b[64];
  std::snprintf(b,sizeof(b),"%0.0f Hz LIVE ESTIMATE",m.tone_hz);
  txt(b,48,573,m.signal?kGreen:kDim,2);
  txt("Tone guide: 1200 sync  /  1500 black  /  1900 middle  /  2300 white",48,616,kDim);
  box(882,kBodyY,381,kBodyH);
  txt("DECODER HEALTH",901,219,kCyan,2);
  std::snprintf(b,sizeof(b),"%lu",(unsigned long)m.samples);
  label("12k SAMPLES",b,901,267);
  std::snprintf(b,sizeof(b),"%lu",(unsigned long)m.audio_overruns);
  label("RING OVERRUNS",b,901,348,m.audio_overruns?kOrange:kWhite);
  std::snprintf(b,sizeof(b),"%lu",(unsigned long)m.good_vis);
  label("VALID VIS HEADERS",b,901,429,kGreen);
  txt("Raw RF IQ is not the SSTV input.",903,566,kDim);
  txt("Decoder sees demodulated 48k PCM.",903,595,kDim);
}
void draw_frequencies() {
  box(18,kBodyY,1244,kBodyH);
  txt("QUICK TUNE / LOCAL + HF",42,220,kCyan,2);
  txt("Preset frequencies are starting points, not activity guarantees.",43,260,kDim);
  for(size_t i=0;i<std::size(kProfiles);++i) {
    const int y=300+static_cast<int>(i)*80;
    const bool on=i==g_profile;
    box(42,y,1194,70,on?kCyan:kBorder,on?0x132D:0x0840);
    char f[44];std::snprintf(f,sizeof(f),"%0.5f MHz",kProfiles[i].hz/1e6);
    txt(kProfiles[i].name,59,y+22,kWhite);
    txt(f,337,y+34,kCyan,2);
    txt(kProfiles[i].usb?"USB":"NFM",607,y+35,kGreen);
    txt(kProfiles[i].description,722,y+35,kDim);
  }
}
void draw_setup() {
  box(18,kBodyY,720,kBodyH);
  txt("RECEIVE CONFIGURATION",44,224,kCyan,2);
  txt("DECODING MODE",44,287,kDim);
  const Mode chosen=selected_mode();
  for(int i=0;i<4;++i) {
    button(42+(i%2)*335,320+(i/2)*87,308,67,
           mode_name(static_cast<Mode>(i)),static_cast<int>(chosen)==i);
  }
  box(753,kBodyY,508,kBodyH);
  txt("CAPTURE + STORAGE",778,224,kCyan,2);
  txt("Completed images remain in PSRAM",779,271,kWhite);
  txt("until the session ends or gallery clears.",779,303,kDim);
  button(778,345,460,66,g_ui.autosave?"AUTO-SAVE TO SD: ON":"AUTO-SAVE TO SD: OFF",g_ui.autosave);
  txt(g_ui.sd_ready?"SD card mounted": "SD card unavailable - gallery remains in RAM",779,452,
      g_ui.sd_ready?kGreen:kOrange);
  txt("Advanced note:",779,529,kDim);
  txt("Live decode is experimental; verify",779,561,kDim);
  txt("images against known SSTV recordings.",779,590,kDim);
}
void draw_all() {
  if(!g_active) return;
  M5.Display.startWrite();
  M5.Display.fillScreen(kBg);
  draw_header(); draw_tabs();
  switch(g_page) {
    case Page::receive: draw_receive();break;
    case Page::gallery: draw_gallery();break;
    case Page::analyzer: draw_analyzer();break;
    case Page::frequencies: draw_frequencies();break;
    case Page::setup: draw_setup();break;
    default:break;
  }
  draw_footer();
  M5.Display.endWrite();
  g_last_render_ms=millis();
}
} // namespace
void ui_enter(const UiSnapshot& snapshot) {
  g_ui=snapshot; g_active=true; g_page=Page::receive; draw_all();
}
void ui_leave() { g_active=false; }
bool ui_active() { return g_active; }
void ui_update(const UiSnapshot& snapshot) {
  if(!g_active) return;
  const bool frequency_changed = g_ui.frequency_hz!=snapshot.frequency_hz;
  g_ui=snapshot;
  const auto m=metrics();
  const uint32_t ms=millis();
  const uint32_t interval=(g_page==Page::receive && m.receiving)?300u:1000u;
  if(frequency_changed || ms-g_last_render_ms>=interval) draw_all();
}
Page ui_page() { return g_page; }
uint8_t ui_profile() { return g_profile; }
bool ui_autosave() { return g_ui.autosave; }
void ui_set_autosave(bool v) { g_ui.autosave=v; draw_all(); }
void ui_notice(const char* msg) {
  if(!msg) msg="";
  std::snprintf(g_notice,sizeof(g_notice),"%s",msg);
  g_notice_until=millis()+4500; draw_all();
}
Action ui_touch(int32_t x,int32_t y) {
  if(!g_active) return {};
  if(hit(x,y,1164,662,101,55)) return {ActionKind::exit,0};
  if(y>=kTabY && y<kTabY+kTabH) {
    for(int i=0;i<5;++i)
      if(hit(x,y,18+i*250,kTabY,242,kTabH)) {
        g_page=static_cast<Page>(i); draw_all(); return {};
      }
  }
  if(g_page==Page::receive) {
    if(hit(x,y,810,554,204,68)) return {ActionKind::abort_rx,0};
    if(hit(x,y,1024,554,214,68)) return {ActionKind::save_image,0};
  } else if(g_page==Page::gallery) {
    if(hit(x,y,892,449,352,65)) return {ActionKind::save_image,static_cast<int32_t>(g_gallery_selected)};
    if(hit(x,y,892,542,352,65)) return {ActionKind::clear_gallery,0};
    if(hit(x,y,35,250,815,335)) {
      for(int i=0;i<4;++i) {
        if(hit(x,y,35+(i%2)*405,250+(i/2)*176,387,155)) {
          g_gallery_selected=i;draw_all();break;
        }
      }
    }
  } else if(g_page==Page::frequencies) {
    for(int i=0;i<static_cast<int>(std::size(kProfiles));++i)
      if(hit(x,y,42,300+i*80,1194,70)) { g_profile=i;draw_all();return {ActionKind::tune_profile,i}; }
  } else if(g_page==Page::setup) {
    for(int i=0;i<4;++i)
      if(hit(x,y,42+(i%2)*335,320+(i/2)*87,308,67)) {
        set_mode(static_cast<Mode>(i));draw_all();return {ActionKind::select_mode,i};
      }
    if(hit(x,y,778,345,460,66)) return {ActionKind::toggle_autosave,0};
  }
  return {};
}
} // namespace orcsdr::sstv

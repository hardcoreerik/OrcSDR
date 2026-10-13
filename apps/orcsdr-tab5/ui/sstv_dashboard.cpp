#include "sstv_dashboard.hpp"
#include <M5Unified.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include "sstv_archive.hpp"
#include "sstv_auto_detect.hpp"
#include "sstv_status.hpp"
#include "time_service.hpp"

namespace orcsdr::sstv {
namespace {
namespace arc = orcsdr::sstv_archive;
using orcsdr::sstv_settings::Detection;
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
static uint16_t g_thumb[100*76];
bool hit(int x,int y,int left,int top,int width,int height) {
  return x>=left && x<left+width && y>=top && y<top+height;
}
void txt(const char* s,int x,int y,uint16_t color=kWhite,int sz=1,
         textdatum_t datum=middle_left) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextColor(color);
  M5.Display.setFont(sz == 1 ? &fonts::DejaVu18 : &fonts::DejaVu24);
  M5.Display.setTextSize(1);
  M5.Display.drawString(s,x,y);
  M5.Display.setFont(nullptr);
}
void box(int x,int y,int w,int h,uint16_t border=kBorder, uint16_t fill=kPanel) {
  M5.Display.fillRoundRect(x,y,w,h,9,fill);
  M5.Display.drawRoundRect(x,y,w,h,9,border);
}
void button(int x,int y,int w,int h,const char* name,bool on=false) {
  box(x,y,w,h,on?kCyan:kBorder,on?0x132D:kPanel);
  txt(name,x+w/2,y+h/2,on?kCyan:kWhite,1,middle_center);
}
uint16_t tone_color(sstv_status::Tone tone) {
  switch(tone) {
    case sstv_status::Tone::good: return kGreen;
    case sstv_status::Tone::warn: return kOrange;
    case sstv_status::Tone::info: return kCyan;
    default: return kDim;
  }
}
const Image* newest_archived(Image* storage) {
  *storage=archived_image(0);
  return storage->pixels?storage:nullptr;
}
// Short save status for one image, from the background archive.
const char* save_text(const Image& im,uint16_t* color) {
  arc::Result r;
  if(im.session_id && arc::result_for(im.session_id,&r)) {
    switch(r.state) {
      case arc::JobState::saved: *color=kGreen;return "SAVED TO SD";
      case arc::JobState::saving: *color=kCyan;return "SAVING...";
      case arc::JobState::pending: *color=kCyan;return "QUEUED FOR SD";
      case arc::JobState::failed: *color=kOrange;return arc::error_name(r.error);
      case arc::JobState::skipped: *color=kDim;return "TOO SHORT TO SAVE";
      default: break;
    }
  }
  *color=kDim;
  return g_ui.settings.autosave?"WAITING":"NOT SAVED (AUTO-SAVE OFF)";
}
void archive_summary(char* out,size_t n) {
  const auto s=arc::stats();
  std::snprintf(out,n,"SD %lu OK / %lu WAIT / %lu ERR",
    (unsigned long)s.saved,(unsigned long)s.pending,(unsigned long)s.failed);
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
  txt(g_ui.streaming ? state_name(m.state) : "RECEIVER IDLE",
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
  Image last;
  const auto head=sstv_status::headline(m,newest_archived(&last));
  txt("ORC SDR  /  SSTV LAB",24,686,kCyan);
  txt(head.text,340,686,tone_color(head.tone));
  char sd[48];
  archive_summary(sd,sizeof(sd));
  txt(sd,860,686,kDim);
  txt("HOME",1214,686,kWhite,1,middle_center);
  M5.Display.drawRoundRect(1165,667,95,43,7,kBorder);
  if(g_notice[0] && millis()<g_notice_until) {
    M5.Display.fillRect(344,672,745,31,kBg);
    txt(g_notice,348,686,kOrange);
  }
}
// The framebuffer lives in PSRAM. This is only a 734-pixel *single-row*
// viewport buffer.  Copy only newly decoded lines, never repaint a frame
// on the 300 ms UI timer; the DSP thread never touches M5GFX.
uint16_t g_preview_line[734]{};
const uint16_t* g_preview_source=nullptr;
uint16_t g_painted_screen_rows=0,g_painted_source_rows=0;
uint32_t g_last_revision=0;
void paint_image_rows(const Image& original,int x,int y,int w,int h) {
  const Image image=original.pixels?original:archived_image(0);
  if(!image.pixels || !image.width || !image.height)return;
  const int view_w=w-22,view_h=h-22;
  const float fit=std::min(static_cast<float>(view_w)/image.width,
                           static_cast<float>(view_h)/image.height);
  const int dw=std::max(1,std::min(view_w,static_cast<int>(fit*image.width)));
  const int dh=std::max(1,std::min(view_h,static_cast<int>(fit*image.height)));
  const int left=x+(w-dw)/2,top=y+(h-dh)/2;
  const uint16_t rows=image.partial?image.decoded_rows:image.height;
  if(g_preview_source!=image.pixels || rows<g_painted_source_rows) {
    M5.Display.fillRect(x+3,y+3,w-6,h-6,0x0840);
    g_preview_source=image.pixels;
    g_painted_screen_rows=0;
  }
  for(int dy=g_painted_screen_rows;dy<dh;++dy) {
    const uint16_t sy=static_cast<uint16_t>(static_cast<uint32_t>(dy)*image.height/dh);
    if(sy>=rows)break;
    for(int dx=0;dx<dw;++dx) {
      const uint16_t sx=static_cast<uint16_t>(static_cast<uint32_t>(dx)*image.width/dw);
      g_preview_line[dx]=image.pixels[static_cast<size_t>(sy)*image.width+sx];
    }
    M5.Display.pushImage(left,top+dy,dw,1,g_preview_line);
    g_painted_screen_rows=dy+1;
  }
  g_painted_source_rows=rows;
}
void image_area(int x,int y,int w,int h,const Image& im,const char* empty) {
  box(x,y,w,h,kBorder,0x0840);
  if(!im.pixels && !archived_image(0).pixels) {
    txt(empty,x+w/2,y+h/2,kDim,1,middle_center);
    return;
  }
  paint_image_rows(im,x,y,w,h);
}
// ---------------------------------------------------------------- RECEIVE
void draw_receive_stats() {
  const auto m=metrics();
  Image last;
  const Image* previous=newest_archived(&last);
  box(788,kBodyY,475,kBodyH);
  txt("RECEPTION",810,217,kCyan,2);
  M5.Display.drawFastHLine(811,247,425,kBorder);
  const auto head=sstv_status::headline(m,previous);
  txt(head.text,812,276,tone_color(head.tone),2);
  char line[96];
  const ModeSpec* spec=m.receiving?mode_spec(m.received_mode):nullptr;
  if(spec) std::snprintf(line,sizeof(line),"MODE   %s   %u x %u",spec->name,spec->width,spec->height);
  else std::snprintf(line,sizeof(line),"MODE   WAITING FOR A TRANSMISSION");
  txt(line,812,319,kWhite);
  if(m.receiving && m.identification==Identification::vis)
    std::snprintf(line,sizeof(line),"HEADER VIS %u  (CONFIRMED)",static_cast<unsigned>(m.vis));
  else if(m.receiving && m.identification==Identification::sync)
    std::snprintf(line,sizeof(line),"NO VIS - SYNC LIKELY (%u)",static_cast<unsigned>(m.identification_score));
  else std::snprintf(line,sizeof(line),"VIS OK %lu   BAD %lu",(unsigned long)m.good_vis,(unsigned long)m.bad_vis);
  txt(line,812,347,m.receiving?kWhite:kDim);
  if(m.receiving)
    std::snprintf(line,sizeof(line),"IMAGE  IN PROGRESS%s",m.start_unknown?" - START ROW UNKNOWN":"");
  else if(previous) {
    sstv_status::ImageChips chips;
    sstv_status::image_chips(*previous,&chips);
    std::snprintf(line,sizeof(line),"LAST   %s",chips.completeness);
  } else std::snprintf(line,sizeof(line),"IMAGE  NONE YET");
  txt(line,812,375,m.receiving&&m.start_unknown?kOrange:kWhite);
  const int meter=m.rows?400*m.row/m.rows:0;
  M5.Display.fillRoundRect(812,396,400,17,6,kBorder);
  if(meter)M5.Display.fillRoundRect(812,396,meter,17,6,m.start_unknown?kOrange:kGreen);
  std::snprintf(line,sizeof(line),"ROWS %u / %u   (%u%%)",(unsigned)m.row,(unsigned)m.rows,
                m.rows?(unsigned)(100u*m.row/m.rows):0u);
  txt(line,812,431,kWhite);
  std::snprintf(line,sizeof(line),"SYNC  LOCKED %u   MISSED %u   ERR %.1f ms",
    (unsigned)m.session_sync_locked,(unsigned)m.session_sync_missed,(double)m.line_error_ms);
  txt(line,812,466,m.session_sync_missed?kOrange:kDim);
  std::snprintf(line,sizeof(line),"AUDIO %.0f Hz   LEVEL %.0f%%   GAPS %lu",
    (double)m.tone_hz,(double)(m.audio_peak*100.f),(unsigned long)m.audio_gaps);
  txt(line,812,494,m.audio_gaps||m.audio_overruns?kOrange:kDim);
  button(810,554,204,68,"ABORT IMAGE",false);
  button(1024,554,214,68,"CAPTURE IMAGE",false);
}
void draw_receive() {
  image_area(18,kBodyY,754,kBodyH,live_image(),"WAITING FOR FIRST IMAGE / VIS HEADER");
  draw_receive_stats();
}
// ---------------------------------------------------------------- IMAGES
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
    txt("NO IMAGES YET",435,430,kDim,1,middle_center);
    txt("Complete and interrupted transmissions are added here automatically",435,472,kDim,1,middle_center);
  } else {
    for(size_t i=0;i<archive_count() && i<4;++i) {
      const int xx=35+(i%2)*405, yy=250+(i/2)*176;
      const auto im=archived_image(i);
      thumbnail(im,xx,yy,387,155);
      sstv_status::ImageChips chips;
      sstv_status::image_chips(im,&chips);
      char line[64];
      std::snprintf(line,sizeof(line),"#%lu  %s",(unsigned long)im.serial,mode_name(im.mode));
      txt(line,xx+121,yy+22,kWhite);
      txt(chips.completeness,xx+121,yy+50,im.partial?kOrange:kGreen);
      std::snprintf(line,sizeof(line),"%s%s",chips.identification,im.start_unknown?"  JOINED":"");
      txt(line,xx+121,yy+78,im.start_unknown?kOrange:kDim);
      uint16_t color=kDim;
      const char* saved=save_text(im,&color);
      txt(saved,xx+121,yy+106,color);
      if(i==g_gallery_selected) M5.Display.drawRoundRect(xx+2,yy+2,383,151,9,kCyan);
    }
  }
  box(874,kBodyY,388,kBodyH);
  txt("SELECTED IMAGE",892,217,kCyan,2);
  const Image im=archived_image(g_gallery_selected);
  if(im.pixels) {
    sstv_status::ImageChips chips;
    sstv_status::image_chips(im,&chips);
    char line[96];
    std::snprintf(line,sizeof(line),"#%lu   %s",(unsigned long)im.serial,mode_name(im.mode));
    txt(line,894,256,kWhite);
    std::snprintf(line,sizeof(line),"%0.5f MHz",im.frequency_hz/1e6);
    txt(line,894,288,kCyan,2);
    char when[28]="TIME NOT VERIFIED";
    if(im.utc_epoch) orcsdr::time_service::format_utc(when,sizeof(when),im.utc_epoch);
    txt(when,894,322,im.utc_epoch?kWhite:kDim);
    txt(chips.identification,894,350,kWhite);
    txt(chips.completeness,894,378,im.partial?kOrange:kGreen);
    if(im.start_unknown) {
      txt("JOINED IN PROGRESS",894,406,kOrange);
      txt("START ROW UNKNOWN",894,430,kOrange);
    } else txt(chips.origin,894,406,kDim);
    std::snprintf(line,sizeof(line),"ENDED %s",chips.ending[0]?chips.ending:"-");
    txt(line,894,458,kDim);
    std::snprintf(line,sizeof(line),"SYNC %u LOCKED / %u MISSED",(unsigned)im.sync_locked,(unsigned)im.sync_missed);
    txt(line,894,486,im.sync_missed?kOrange:kDim);
    uint16_t color=kDim;
    const char* saved=save_text(im,&color);
    arc::Result r;
    std::snprintf(line,sizeof(line),"SD  %s",saved);
    txt(line,894,514,color);
    if(im.session_id && arc::result_for(im.session_id,&r) && r.filename[0])
      txt(r.filename,894,538,kDim);
  } else txt("NOTHING SELECTED",894,312,kDim);
  button(892,556,352,40,"SAVE BMP TO SD");
  button(892,602,352,40,"CLEAR RAM GALLERY");
}
// ---------------------------------------------------------------- SIGNAL
void draw_analyzer() {
  box(18,kBodyY,850,kBodyH);
  txt("AUDIO TONE MONITOR",43,220,kCyan,2);
  const int px=48,py=265,pw=782,ph=250;
  M5.Display.fillRect(px,py,pw,ph,0x0840);
  for(int i=0;i<5;++i) {
    const int yy=py+24+i*50;
    M5.Display.drawFastHLine(px,yy,pw,kBorder);
  }
  const Metrics m=metrics();
  const int center=py+ph-(std::clamp(m.tone_hz,600.f,2500.f)-600.f)*ph/1900.f;
  M5.Display.drawFastHLine(px+4,center,pw-8,m.signal?kCyan:kDim);
  char b[96];
  std::snprintf(b,sizeof(b),"%0.0f Hz LIVE   LEVEL %.0f%%",m.tone_hz,m.audio_peak*100.f);
  txt(b,48,545,m.signal?kGreen:kDim,2);
  std::snprintf(b,sizeof(b),"SYNC PULSE %.1f ms   EVERY %.1f ms   LINE ERROR %.1f ms",
    (double)m.sync_pulse_ms,(double)m.sync_interval_ms,(double)m.line_error_ms);
  txt(b,48,586,kWhite);
  txt("Tone guide: 1200 sync / 1500 black / 1900 middle / 2300 white",48,620,kDim);
  box(882,kBodyY,381,kBodyH);
  txt("DECODER",901,217,kCyan,2);
  int y=250;
  auto emit=[&](uint16_t color){ txt(b,901,y,color); y+=27; };
  std::snprintf(b,sizeof(b),"STATE  %s",state_name(m.state));
  emit(kWhite);
  std::snprintf(b,sizeof(b),"VIS  OK %lu  BAD %lu  LAST %u",(unsigned long)m.good_vis,(unsigned long)m.bad_vis,(unsigned)m.vis);
  emit(kDim);
  auto_detect::Ranked top[3];
  const size_t shown=auto_detect::ranked(top,3);
  if(shown) {
    for(size_t i=0;i<shown;++i) {
      std::snprintf(b,sizeof(b),"CAND %u  %s  %u",(unsigned)(i+1),mode_name(top[i].mode),(unsigned)top[i].score);
      emit(i==0?kGreen:kDim);
    }
  } else {
    std::snprintf(b,sizeof(b),"CANDIDATES  NONE YET");
    emit(kDim);
  }
  std::snprintf(b,sizeof(b),"MISSED LINES  %u (THIS IMAGE)",(unsigned)m.session_sync_missed);
  emit(m.session_sync_missed?kOrange:kDim);
  std::snprintf(b,sizeof(b),"RING OVERRUNS %lu  GAPS %lu",(unsigned long)m.audio_overruns,(unsigned long)m.audio_gaps);
  emit(m.audio_overruns?kOrange:kDim);
  std::snprintf(b,sizeof(b),"DECODE  AVG %lu us  MAX %lu us",(unsigned long)m.service_us_avg,(unsigned long)m.service_us_max);
  emit(kDim);
  std::snprintf(b,sizeof(b),"CPU %u%%   RING MAX %u / 4096",(unsigned)m.service_cpu_pct,(unsigned)m.ring_fill_max);
  emit(kDim);
  std::snprintf(b,sizeof(b),"IMAGES %lu  PSRAM %lu KB",(unsigned long)m.frames_alive,(unsigned long)(m.frame_bytes/1024u));
  emit(kDim);
  const auto s=arc::stats();
  std::snprintf(b,sizeof(b),"SD WRITE  LAST %lu  MAX %lu ms",(unsigned long)s.last_write_ms,(unsigned long)s.max_write_ms);
  emit(kDim);
  std::snprintf(b,sizeof(b),"SD  %lu SAVED  %lu FAILED",(unsigned long)s.saved,(unsigned long)s.failed);
  emit(s.failed?kOrange:kDim);
}
// ---------------------------------------------------------------- FREQUENCIES
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
// ---------------------------------------------------------------- SETUP
constexpr int kGridX=35,kGridY=292,kGridDx=230,kGridDy=66,kGridW=214,kGridH=56;
void draw_setup() {
  const auto& s=g_ui.settings;
  box(18,kBodyY,720,kBodyH);
  txt("MANUAL MODE  (VIS FILTER)",44,224,kCyan,2);
  txt("Starts an image only when a VIS header names this mode. No VIS, no image.",44,262,kDim);
  for(int i=1;i<=14;++i) {
    const int col=(i-1)%3,row=(i-1)/3;
    button(kGridX+col*kGridDx,kGridY+row*kGridDy,kGridW,kGridH,
           mode_name(static_cast<Mode>(i)),
           s.detection==Detection::manual && static_cast<int>(s.manual_mode)==i);
  }
  box(753,kBodyY,508,kBodyH);
  txt("DETECTION",778,224,kCyan,2);
  button(778,244,460,52,"AUTO: VIS + SYNC RECOVERY (RECOMMENDED)",s.detection==Detection::automatic);
  button(778,304,460,52,"VIS ONLY (NO SYNC FALLBACK)",s.detection==Detection::vis_only);
  char manual[64];
  std::snprintf(manual,sizeof(manual),"MANUAL: %s",mode_name(s.manual_mode));
  button(778,364,460,52,manual,s.detection==Detection::manual);
  txt("STORAGE",778,438,kCyan);
  button(778,456,460,52,s.autosave?"AUTO-SAVE TO SD: ON":"AUTO-SAVE TO SD: OFF",s.autosave);
  const auto st=arc::stats();
  char line[96];
  txt(g_ui.sd_ready?"SD card mounted":"SD unavailable - images stay in memory",779,534,
      g_ui.sd_ready?kGreen:kOrange);
  std::snprintf(line,sizeof(line),"Saved %lu   Waiting %lu   Failed %lu",
    (unsigned long)st.saved,(unsigned long)st.pending,(unsigned long)st.failed);
  txt(line,779,562,kDim);
  if(st.last_error!=arc::Error::none) {
    std::snprintf(line,sizeof(line),"Last problem: %s",arc::error_name(st.last_error));
    txt(line,779,590,kOrange);
  } else txt("/orcsdr/sstv/index.csv (schema v2)",779,590,kDim);
  txt("14 modes, monochrome preview (colour is not decoded)",779,618,kDim);
}
void draw_page_body() {
  switch(g_page) {
    case Page::receive: draw_receive();break;
    case Page::gallery: draw_gallery();break;
    case Page::analyzer: draw_analyzer();break;
    case Page::frequencies: draw_frequencies();break;
    case Page::setup: draw_setup();break;
    default:break;
  }
}
void draw_all() {
  if(!g_active) return;
  M5.Display.startWrite();
  g_preview_source=nullptr;g_painted_screen_rows=0;g_painted_source_rows=0;
  M5.Display.fillScreen(kBg);
  draw_header(); draw_tabs();
  draw_page_body();
  draw_footer();
  M5.Display.endWrite();
  g_last_render_ms=millis();
  g_last_revision=image_revision();
}
} // namespace
void ui_enter(const UiSnapshot& snapshot) {
  g_ui=snapshot; g_active=true; g_page=Page::receive; draw_all();
}
void ui_leave() { g_active=false; }
bool ui_active() { return g_active; }
void ui_update(const UiSnapshot& snapshot) {
  if(!g_active)return;
  const bool retuned=g_ui.frequency_hz!=snapshot.frequency_hz;
  const bool changed=std::memcmp(&g_ui.settings,&snapshot.settings,sizeof(g_ui.settings))!=0 ||
                     g_ui.sd_ready!=snapshot.sd_ready;
  g_ui=snapshot;
  if(retuned||changed){draw_all();return;}
  const uint32_t revision=image_revision();
  const uint32_t now=millis();
  if(g_page==Page::receive) {
    M5.Display.startWrite();
    if(revision!=g_last_revision) {
      paint_image_rows(live_image(),18,kBodyY,754,kBodyH);
      g_last_revision=revision;
    }
    if(now-g_last_render_ms>=1000u){
      draw_receive_stats();draw_header();draw_footer();
      g_last_render_ms=now;
    }
    M5.Display.endWrite();
  } else if(g_page==Page::frequencies) {
    if(now-g_last_render_ms>=3000u){draw_header();draw_footer();g_last_render_ms=now;}
  } else if(revision!=g_last_revision) draw_all();
  else if(now-g_last_render_ms>=1000u) {
    // Live pages refresh their body once a second without repainting the whole screen.
    M5.Display.startWrite();
    draw_page_body();draw_header();draw_footer();
    M5.Display.endWrite();
    g_last_render_ms=now;
  }
}
Page ui_page() { return g_page; }
uint8_t ui_profile() { return g_profile; }
void ui_set_profile(uint8_t index) { g_profile=index; if(g_active)draw_all(); }
void ui_set_settings(const sstv_settings::Settings& settings) { g_ui.settings=settings; draw_all(); }
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
    if(hit(x,y,1024,554,214,68)) return {ActionKind::save_image,live_image().decoded_rows>0 ? -1 : 0};
  } else if(g_page==Page::gallery) {
    if(hit(x,y,892,556,352,40)) return {ActionKind::save_image,static_cast<int32_t>(g_gallery_selected)};
    if(hit(x,y,892,602,352,40)) return {ActionKind::clear_gallery,0};
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
    for(int i=1;i<=14;++i)
      if(hit(x,y,kGridX+((i-1)%3)*kGridDx,kGridY+((i-1)/3)*kGridDy,kGridW,kGridH))
        return {ActionKind::select_mode,i};
    if(hit(x,y,778,244,460,52)) return {ActionKind::set_detection,static_cast<int32_t>(Detection::automatic)};
    if(hit(x,y,778,304,460,52)) return {ActionKind::set_detection,static_cast<int32_t>(Detection::vis_only)};
    if(hit(x,y,778,364,460,52)) return {ActionKind::select_mode,static_cast<int32_t>(g_ui.settings.manual_mode)};
    if(hit(x,y,778,456,460,52)) return {ActionKind::toggle_autosave,0};
  }
  return {};
}
} // namespace orcsdr::sstv

#include "hfdl_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include "orc_badge.hpp"
#include <M5Unified.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::hfdl_dashboard {
namespace {
hfdl_ui::Model* g_model=nullptr;
hfdl_ui::Controller* g_controller=nullptr;
HeaderHook g_header=nullptr;
bool (*g_overlay_open)()=nullptr;
hfdl_ui::layout::View g_view{};
bool g_active=false;
uint32_t g_snapshot_revision=0,g_controller_revision=0;
constexpr uint16_t kBg=0x0000,kPanel=0x0841,kCyan=0x2e7f;
constexpr uint16_t kGreen=0x6fe8,kYellow=0xff24,kMuted=0x8c71;
constexpr uint16_t kGrid=0x2945,kWhite=0xffff;
using Rect=hfdl_ui::layout::Rect;
void text(const char* s,int x,int y,uint16_t color,int size=2) {
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(color,kPanel);
  M5.Display.setTextSize(size);
  M5.Display.drawString(s,x,y);
}
void panel(Rect r,uint16_t outline=kGrid) {
  if(r.w<=0||r.h<=0)return;
  M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,8,kPanel);
  M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,8,outline);
}
void button(Rect r,const char* label,bool enabled) {
  panel(r,enabled?kCyan:kGrid);
  text(label,r.x+12,r.y+r.h/2,enabled?kWhite:kMuted,r.w<140?1:2);
}
void clipped_text(const char* src,int x,int y,int max_width,uint16_t color,int size=1) {
  char buf[96]{};
  const size_t cap=std::min<size_t>(sizeof(buf)-1,
                      static_cast<size_t>(std::max(0,max_width/(6*size))));
  if(src && cap>0) {
    size_t i=0;
    while(i<cap && src[i] && src[i]!='\n' && src[i]!='\r') {
      const unsigned char c=static_cast<unsigned char>(src[i]);
      buf[i]=c>=32&&c<127?static_cast<char>(c):'.';++i;
    }
    buf[i]=0;
  }
  text(buf,x,y,color,size);
}
void draw_header() {
  if(g_view.panel==hfdl_ui::layout::Panel::tab5) {
    // Same brand/status reserve as the existing aviation dashboards.
    orcsdr::audio_header::draw_brand("HF DATA LINK");
    if(g_header)g_header();
    return;
  }
  M5.Display.fillRect(g_view.header.x,g_view.header.y,g_view.header.w,g_view.header.h,kBg);
  const int badge_size=(g_view.portrait?72:44);
  const int bx=10,by=8;
  bool badge_ok=false;
#ifndef ORCSDR_HFDL_HOST
  badge_ok=orcsdr::badge::draw(bx,by,badge_size);
#endif
  if(!badge_ok) {
    M5.Display.drawRoundRect(bx,by,badge_size,badge_size,6,kGreen);
  }
  const int left=bx+badge_size+12;
  text("ORCSDR",left,g_view.header.y+18,kGreen,g_view.portrait?3:2);
  text("HFDL / HF AVIATION",left,g_view.header.y+(g_view.portrait?57:38),kCyan,1);
  button(g_view.home,"HOME",true);
  if(g_view.portrait)
    text("RECEIVE ONLY",left,g_view.header.y+96,kMuted,1);
}
void draw_summary(const hfdl_ui::Snapshot& snap) {
  panel(g_view.summary,kCyan);
  char s[64]{};
  const int left=g_view.summary.x+12;
  if(g_view.panel==hfdl_ui::layout::Panel::hosyond800){
    std::snprintf(s,sizeof(s),"VERIFIED %lu",(unsigned long)snap.messages_total);
    text(s,left,g_view.summary.y+18,kGreen,1);
    std::snprintf(s,sizeof(s),"QUEUE DROP %lu",(unsigned long)snap.queue_drops);
    text(s,left+280,g_view.summary.y+18,snap.queue_drops?kYellow:kMuted,1);
    text(snap.receiver==hfdl_ui::ReceiverState::running?"FEED RUNNING":
         snap.receiver==hfdl_ui::ReceiverState::error?"FEED ERROR":"FEED IDLE",
         left+530,g_view.summary.y+18,kCyan,1);
    return;
  }
  text("HFDL / VERIFIED EVENTS",left,g_view.summary.y+22,kCyan,1);
  std::snprintf(s,sizeof(s),"%lu DECODED",(unsigned long)snap.messages_total);
  text(s,left,g_view.summary.y+52,kGreen,2);
  std::snprintf(s,sizeof(s),"DROPPED %lu",(unsigned long)snap.queue_drops);
  text(s,left+std::min(360,g_view.summary.w/2),g_view.summary.y+52,
       snap.queue_drops?kYellow:kMuted,1);
  if(g_view.summary.w>=1000) {
    text(snap.receiver==hfdl_ui::ReceiverState::running?"INPUT ACTIVE":
         snap.receiver==hfdl_ui::ReceiverState::error?"INPUT ERROR":"INPUT IDLE",
         left+750,g_view.summary.y+52,kMuted,2);
  }
  if(g_view.portrait)
    text("CRC-VALID INPUT ONLY",left,g_view.summary.y+96,kMuted,1);
}
void draw_aside(const hfdl_ui::Snapshot& snap) {
  if(g_view.aside.w<=0)return;
  panel(g_view.aside,kGrid);
  const int x=g_view.aside.x+14,y=g_view.aside.y;
  text("RECEIVER / STATUS",x,y+26,kCyan,g_view.aside.w<300?1:2);
  text(snap.receiver==hfdl_ui::ReceiverState::running?"SAMPLES RECEIVED":
       snap.receiver==hfdl_ui::ReceiverState::error?"RECEIVER ERROR":"NO INPUT CONNECTED",
       x,y+70,snap.receiver==hfdl_ui::ReceiverState::error?kYellow:kMuted,1);
  text("HFDL PHY IN DEVELOPMENT",x,y+114,kYellow,1);
  if(g_view.aside.h>250){
    text("No inferred aircraft positions",x,y+165,kMuted,1);
    text("No fabricated squitters",x,y+196,kMuted,1);
    text("Only verified frames displayed",x,y+227,kGreen,1);
  }
}
void draw_list(const hfdl_ui::Snapshot& snap) {
  panel(g_view.list,kGrid);
  text("LIVE / RECENT MESSAGES",g_view.list.x+14,g_view.list.y+21,kCyan,1);
  if(snap.message_count==0) {
    clipped_text("NO VERIFIED HFDL FRAMES",g_view.list.x+18,g_view.list.y+g_view.list.h/2-14,
                 g_view.list.w-36,kYellow,2);
    clipped_text("Decoder PHY not yet implemented",g_view.list.x+18,
                 g_view.list.y+g_view.list.h/2+26,g_view.list.w-36,kMuted,1);
  }
  for(size_t i=0;i<snap.message_count && i<g_view.visible_rows;++i) {
    const auto r=g_view.rows[i];
    const auto& m=snap.messages[i];
    panel(r,kGrid);
    char line[80]{};
    std::snprintf(line,sizeof(line),"#%lu   %.3f MHz   %s   %s",
                  (unsigned long)m.seq,m.frequency_hz/1000000.0,m.label,
                  m.integrity==hfdl_ui::Integrity::crc_ok?"CRC OK":"FEC / CRC OK");
    clipped_text(line,r.x+10,r.y+15,r.w-20,kGreen,1);
    clipped_text(m.preview,r.x+10,r.y+r.h-17,r.w-20,kWhite,1);
  }
  draw_aside(snap);
}
void draw_detail(const hfdl_ui::MessageRecord& m){
  panel(g_view.detail,kCyan);
  const int x=g_view.detail.x+14,y=g_view.detail.y;
  text("HFDL / MESSAGE DETAIL",x,y+28,kCyan,1);
  button(g_view.close,"BACK",true);
  if(g_view.detail.h<300){
    char compact[112]{};
    std::snprintf(compact,sizeof(compact),"#%lu  %.3f MHz  %s",
                  (unsigned long)m.seq,m.frequency_hz/1000000.0,m.label);
    clipped_text(compact,x,y+82,g_view.detail.w-28,kGreen,2);
    clipped_text(m.preview,x,y+127,g_view.detail.w-28,kWhite,1);
    clipped_text("CRC-VERIFIED MESSAGE (PAYLOAD IN LONG VIEW)",x,y+173,
                 g_view.detail.w-28,kMuted,1);
    return;
  }
  char line[104]{};
  std::snprintf(line,sizeof(line),"SEQ #%lu / %.3f MHz",
                (unsigned long)m.seq,m.frequency_hz/1000000.0);
  clipped_text(line,x,y+86,g_view.detail.w-28,kGreen,2);
  std::snprintf(line,sizeof(line),"LABEL %s  AIRCRAFT %s  %s",
                m.label,m.tail,
                m.integrity==hfdl_ui::Integrity::crc_ok?"CRC VERIFIED":"FEC / CRC VERIFIED");
  clipped_text(line,x,y+134,g_view.detail.w-28,kWhite,1);
  clipped_text(m.preview,x,y+182,g_view.detail.w-28,kYellow,1);
  text("PAYLOAD (PRINTABLE VIEW)",x,y+222,kMuted,1);
  const size_t maxlen=std::min<size_t>(m.text_length,sizeof(m.text));
  const int chars_per_line=std::max(1,std::min(75,(g_view.detail.w-28)/7));
  const int maxlines=std::max(0,(g_view.detail.h-260)/22);
  for(int row=0;row<maxlines && static_cast<size_t>(row*chars_per_line)<maxlen;++row){
    char buf[80]{};
    const size_t start=static_cast<size_t>(row*chars_per_line);
    const size_t n=std::min<size_t>(maxlen-start,
                       std::min<size_t>(sizeof(buf)-1,static_cast<size_t>(chars_per_line)));
    for(size_t j=0;j<n;++j){
      const unsigned char c=static_cast<unsigned char>(m.text[start+j]);
      buf[j]=c>=32&&c<127?static_cast<char>(c):'.';
    }
    text(buf,x,y+250+22*row,kWhite,1);
  }
}
void draw_footer(const hfdl_ui::Snapshot& snap){
  panel(g_view.footer,kGrid);
  if(g_controller && g_controller->detail_open()){
    text("TAP BACK TO LIVE",g_view.footer.x+14,g_view.footer.y+g_view.footer.h/2,kMuted,1);
    return;
  }
  button(g_view.newer,"NEWER",snap.message_offset>0);
  button(g_view.older,"OLDER",snap.message_offset+g_view.visible_rows<snap.messages_held);
}
void draw_content(const hfdl_ui::Snapshot& snap){
  if(g_controller && g_controller->detail_open())
    draw_detail(g_controller->detail_record());
  else draw_list(snap);
}
} // namespace

void install(hfdl_ui::Model* model,hfdl_ui::Controller* controller,HeaderHook hook){
  g_model=model;g_controller=controller;g_header=hook;
}
void set_overlay_probe(bool(*probe)()){g_overlay_open=probe;}
void enter(){g_active=true;g_snapshot_revision=0;g_controller_revision=0;draw();}
void leave(){g_active=false;}
void draw(){
  if(!g_active||!g_model||!g_controller)return;
  g_view=hfdl_ui::layout::make_view(M5.Display.width(),M5.Display.height());
  if(!g_view.valid)return;  // Do not render off-panel on unknown displays.
  g_model->service();
  M5.Display.fillScreen(kBg); // allowed only at structural transition.
  draw_header();
  const auto& snap=g_model->snapshot();
  draw_summary(snap);
  draw_content(snap);
  draw_footer(snap);
  g_snapshot_revision=snap.revision;
  g_controller_revision=g_controller->revision();
}
void update(uint32_t /*now_ms*/){
  if(!g_active||!g_model||!g_controller||!g_view.valid)return;
  g_model->service();
  if(g_overlay_open && g_overlay_open())return;
  const auto& snap=g_model->snapshot();
  const bool data=snap.revision!=g_snapshot_revision;
  const bool page=g_controller->revision()!=g_controller_revision;
  if(!data && !page)return;
  if(data)draw_summary(snap);
  draw_content(snap);  // only content bounds, never full-screen clear
  draw_footer(snap);
  g_snapshot_revision=snap.revision;
  g_controller_revision=g_controller->revision();
}
hfdl_ui::Action touch(int32_t x,int32_t y){
  if(!g_active||!g_model||!g_controller||!g_view.valid)return {};
  const auto action=g_controller->touch(x,y,*g_model,g_view);
  if(action.kind==hfdl_ui::ActionKind::changed)update(0);
  return action;
}
bool active(){return g_active;}
const hfdl_ui::layout::View& viewport(){return g_view;}
}  // namespace orcsdr::hfdl_dashboard

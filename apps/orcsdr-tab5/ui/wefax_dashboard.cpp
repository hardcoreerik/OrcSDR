#include "wefax_dashboard.hpp"
#include "wefax_schedule.hpp"
#include <M5Unified.h>
#include <cstdio>
namespace orcsdr::wefax_dashboard {
namespace {
namespace cat = orcsdr::wefax_schedule;
constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kPanel = 0x1082;
constexpr uint16_t kBorder = 0x2e7f;
constexpr uint16_t kMuted = 0x9cd3;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kAmber = 0xfd20;
struct Rect { int x, y, w, h; bool contains(int x0, int y0) const { return x0>=x && y0>=y && x0<x+w && y0<y+h; } };
Snapshot g_s{};
bool g_active = false;
uint8_t g_station=0, g_frequency=1; // NMC at 8682 kHz assigned (8680.1 USB)
void (*g_header)() = nullptr;
int g_width=1280, g_height=720;
Rect g_station_bar{}, g_left{}, g_right{}, g_prev{}, g_next{}, g_tune{}, g_station0{}, g_station1{};
uint64_t g_last_utc_minute=0;
uint32_t g_last_revision=0;

void label(const char* s, int x, int y, uint16_t color, int size=2) {
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color, kPanel);
  M5.Display.drawString(s,x,y);
}
void frame(const Rect& r) {
  M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,9,kPanel);
  M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,9,kBorder);
}
void button(const Rect& r,const char* s,bool selected=true) {
  M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,6,selected?0x1264:kPanel);
  M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,6,selected?kGreen:kBorder);
  label(s,r.x+10,r.y+7,selected?kGreen:TFT_WHITE,2);
}
void geometry() {
  g_width=M5.Display.width(); g_height=M5.Display.height();
  // Shared header occupies the top 96 px, Home and Settings are the caller's.
  g_station_bar={18,100,g_width-36,54};
  const int top=166, bottom=g_height-18;
  if(g_width>=900) {
    const int leftw=(g_width-54)*42/100;
    g_left={18,top,leftw,bottom-top};
    g_right={g_left.x+g_left.w+18,top,g_width-g_left.w-54,bottom-top};
  } else { // 720x720, portrait or square; no hardwired 1280-px assumptions
    g_left={18,top,g_width-36,210};
    g_right={18,top+220,g_width-36,bottom-(top+220)};
  }
  const int control_y=g_left.y+g_left.h-57;
  g_prev={g_left.x+16,control_y,75,40};
  g_next={g_left.x+101,control_y,75,40};
  g_tune={g_left.x+190,control_y,g_left.w-206,40};
  g_station0={g_station_bar.x+10,g_station_bar.y+9,(g_station_bar.w-30)/2,37};
  g_station1={g_station0.x+g_station0.w+10,g_station_bar.y+9,g_station0.w,37};
}
void draw_header() {
  M5.Display.fillRect(0,0,g_width,96,kBg);
  M5.Display.setTextSize(g_width<900?2:3);
  M5.Display.setTextColor(kGreen,kBg);
  M5.Display.drawString("WEATHER FAX",22,22);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(kMuted,kBg);
  M5.Display.drawString("HF RADIOFAX  -  OFFLINE SCHEDULE GUIDE",22,66);
  if(g_header) g_header();
}
void station_panel() {
  frame(g_station_bar);
  button(g_station0,"NMC  PACIFIC",g_station==0);
  button(g_station1,"NMF  ATLANTIC",g_station==1);
  frame(g_left);
  const auto* s=cat::station(g_station);
  const auto* f=cat::frequency(g_station,g_frequency);
  if(!s || !f)return;
  char line[90];
  label(s->name,g_left.x+16,g_left.y+13,kGreen,2);
  std::snprintf(line,sizeof line,"ASSIGNED   %.4f MHz",f->assigned_hz/1000000.0);
  label(line,g_left.x+16,g_left.y+46,kMuted,2);
  std::snprintf(line,sizeof line,"USB DIAL   %.4f MHz",cat::usb_dial_hz(f->assigned_hz)/1000000.0);
  label(line,g_left.x+16,g_left.y+79,TFT_WHITE,2);
  if(g_left.h>270) {
    label(f->published==cat::Daypart::day?"NOAA: DAY" : f->published==cat::Daypart::night?"NOAA: NIGHT":"NOAA: DAY / NIGHT",
          g_left.x+16,g_left.y+113,kMuted,2);
    label("Assigned - 1.9 kHz  |  USB",g_left.x+16,g_left.y+149,kMuted,2);
    label("Schedule != signal detected",g_left.x+16,g_left.y+181,kAmber,2);
  }
  button(g_prev,"< FREQ",false);
  button(g_next,"FREQ >",false);
  button(g_tune,g_s.receiver_ready?"TUNE USB":"NO RTL-SDR",g_s.receiver_ready);
}
void schedule_panel() {
  frame(g_right);
  const bool clock=g_s.verified_utc_epoch!=0;
  label("UPCOMING PUBLISHED SLOTS",g_right.x+14,g_right.y+12,kGreen,2);
  const int font_h=28;
  int y=g_right.y+45;
  if(!clock) {
    label("UTC CLOCK UNVERIFIED",g_right.x+14,y,kAmber,2);
    y+=35;
    label("Next times unavailable",g_right.x+14,y,kMuted,2);
  } else {
    cat::Upcoming next[5]{};
    const size_t count=cat::next_broadcasts(g_s.verified_utc_epoch,g_station,next,5);
    for(size_t i=0;i<count && y+font_h<g_right.y+g_right.h-40;++i) {
      char t[8],line[96];
      cat::format_utc_minute(next[i].broadcast->utc_minute,t,sizeof t);
      const auto mins=(next[i].start_utc-g_s.verified_utc_epoch+59)/60;
      if(g_width>=900)
        std::snprintf(line,sizeof line,"%s  %s  +%llum",t,next[i].broadcast->title,static_cast<unsigned long long>(mins));
      else
        std::snprintf(line,sizeof line,"%s  %s",t,next[i].broadcast->title);
      label(line,g_right.x+14,y,TFT_WHITE,g_width<900?1:2);
      y+=g_width<900?27:38;
    }
  }
  // This is NOT a full NOAA schedule or a verified reception planner.
  label("CURATED SLOTS / UTC / NOT LIVE",g_right.x+14,g_right.y+g_right.h-30,kAmber,1);
}
void draw_body() {
  // Only the feature area is redrawn; no full-screen clear on ordinary ticks.
  M5.Display.fillRect(0,96,g_width,g_height-96,kBg);
  station_panel();
  schedule_panel();
}
} // namespace
void set_header_hook(void(*hook)()){g_header=hook;}
void enter(const Snapshot& snapshot) {
  g_s=snapshot;
  g_active=true;
  geometry();
  draw_header();
  draw_body();
  g_last_utc_minute=g_s.verified_utc_epoch/60;
  g_last_revision=g_s.revision;
}
void update(const Snapshot& snapshot) {
  if(!g_active)return;
  const bool state_changed=g_s.receiver_ready!=snapshot.receiver_ready ||
      g_s.tuned_hz!=snapshot.tuned_hz ||
      g_s.decoder_connected!=snapshot.decoder_connected ||
      g_s.wefax_receiving!=snapshot.wefax_receiving ||
      g_s.received_rows!=snapshot.received_rows ||
      g_last_revision!=snapshot.revision;
  const auto next_minute=snapshot.verified_utc_epoch/60;
  const bool clock_changed=(next_minute!=g_last_utc_minute) ||
        (bool(g_s.verified_utc_epoch)!=bool(snapshot.verified_utc_epoch));
  g_s=snapshot;
  g_last_revision=snapshot.revision;
  if(state_changed)station_panel();
  if(clock_changed)schedule_panel();
  g_last_utc_minute=next_minute;
}
void leave(){g_active=false;}
bool active(){return g_active;}
Action touch(int32_t x,int32_t y) {
  if(!g_active || y<96)return {};
  if(g_station0.contains(x,y) || g_station1.contains(x,y)) {
    const uint8_t selected=g_station1.contains(x,y)?1:0;
    if(g_station!=selected) {g_station=selected;g_frequency=selected==0?1:0;draw_body();}
    return {};
  }
  const auto* s=cat::station(g_station);
  if(!s)return {};
  if(g_prev.contains(x,y) || g_next.contains(x,y)) {
    if(g_next.contains(x,y))g_frequency=static_cast<uint8_t>((g_frequency+1)%s->frequency_count);
    else g_frequency=static_cast<uint8_t>((g_frequency+s->frequency_count-1)%s->frequency_count);
    station_panel();
    return {};
  }
  if(g_tune.contains(x,y) && g_s.receiver_ready) {
    const auto* f=cat::frequency(g_station,g_frequency);
    if(f) return {ActionKind::tune_usb,cat::usb_dial_hz(f->assigned_hz)};
  }
  return {};
}
} // namespace orcsdr::wefax_dashboard

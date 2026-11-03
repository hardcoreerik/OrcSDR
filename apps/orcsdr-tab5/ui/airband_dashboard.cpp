#include "focus_nav.hpp"
#include "airband_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "spectrum_resample.hpp"
#include "waterfall_style.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::airband {
namespace {

constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kGrid = 0x2945;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kAmber = 0xfd20;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kSelected = 0x1264;

constexpr int kTabCount = 6;
using Rect = layout::Rect;
layout::Layout g_layout = layout::make_layout({0,0,1280,720});
const int& kTabsY = g_layout.tabs.y;
const int& kScopeX = g_layout.scope_plot.x;
const int& kScopeW = g_layout.scope_plot.w;
const int& kScopeSpecY = g_layout.scope_plot.y;
const int& kScopeSpecH = g_layout.scope_plot.h;
const int& kScopeAxisY = g_layout.scope_axis.y;
const int& kScopeAxisH = g_layout.scope_axis.h;
const int& kScopeWfY = g_layout.scope_waterfall.y;
const int& kScopeWfH = g_layout.scope_waterfall.h;
const int& kScopeRowAY = g_layout.scope_control[0].y;
const int& kScopeRowBY = g_layout.scope_control[6].y;
const int& kScopeRowH = g_layout.scope_control[0].h;
Rect scope_minus(int group, int row_y) { const int base=row_y==g_layout.scope_control[6].y?6:0; return g_layout.scope_control[base+group*2]; }
Rect scope_value(int group, int row_y) { const int base=row_y==g_layout.scope_control[6].y?6:0; return {g_layout.scope_control[base+group*2].x+68,row_y, g_layout.scope_control[base+group*2+1].x-g_layout.scope_control[base+group*2].x-72,g_layout.scope_control[base+group*2].h}; }
Rect scope_plus(int group, int row_y) { const int base=row_y==g_layout.scope_control[6].y?6:0; return g_layout.scope_control[base+group*2+1]; }
const Rect& kScopeAgc = g_layout.scope_control[10];
const Rect& kScopeScan = g_layout.scope_control[11];
const Rect& kScopeGuard = g_layout.scope_control[12];
const Rect& kScopePaletteChip = g_layout.scope_palette;
const Rect& kScopeSpeedChip = g_layout.scope_speed;
constexpr float kScopeMinRangeDb = 24.0f;
constexpr float kScopeHeadroomDb = 5.0f;
constexpr float kScopeWaterfallRangeDb = 18.0f;

const Rect& kNowCard = g_layout.listen_now;
const Rect& kControlsCard = g_layout.listen_controls;
const Rect& kStatusCard = g_layout.listen_status;
const Rect& kGainCard = g_layout.listen_gain;
const Rect& kGainAuto = g_layout.gain_auto;
const Rect& kGainDown = g_layout.gain_down;
const Rect& kGainValue = g_layout.gain_value;
const Rect& kGainUp = g_layout.gain_up;
const Rect& kRtlAgc = g_layout.rtl_agc;
constexpr int kListRows = 12;
constexpr int kSetupRows = 7;

EXT_RAM_BSS_ATTR static Snapshot g_snapshot{};
bool g_active = false;
Tab g_tab = Tab::listen;
size_t g_bank_page = 0, g_airport_page = 0, g_activity_page = 0;
uint8_t g_setup_page=0;
enum class DetailKind : uint8_t { none, bank, catalog, activity };
DetailKind g_detail_kind=DetailKind::none;
size_t g_detail_index=0;
EXT_RAM_BSS_ATTR uint16_t g_scope_row[1232]{};
EXT_RAM_BSS_ATTR float g_scope_levels[1232]{};
uint32_t g_scope_span_hz = 0;
void (*g_scope_span_hook)(uint32_t hz) = nullptr;
constexpr uint32_t kScopeDefaultSpanHz = 480000;
bool g_scope_levels_valid = false;
M5Canvas* g_scope_canvas = nullptr;
int g_scope_canvas_w = 0, g_scope_canvas_h = 0;
constexpr float kScopeSmoothing = 0.45f;   // weight of the new frame (the FM trace uses 0.22)

// Entering the scope narrows the shared span; leaving it restores the previous one.
void set_tab(Tab next) {
  if (g_tab == Tab::scope && next != Tab::scope) {
    M5.Display.clearScrollRect();
    if (g_scope_span_hook) g_scope_span_hook(0);
  } else if (g_tab != Tab::scope && next == Tab::scope) {
    g_scope_span_hz = 0;
    g_scope_levels_valid = false;
    if (g_scope_span_hook) g_scope_span_hook(kScopeDefaultSpanHz);
  }
  g_tab = next;
}
uint32_t g_scope_fps = 0, g_scope_frames = 0, g_scope_window_ms = 0, g_scope_draw_ms = 0;
float g_scope_ceiling = 0.0f;
bool g_scope_ceiling_valid = false;
uint32_t g_last_activity_redraw_ms = 0;
// What the level meter currently shows, so a slow drift still triggers a repaint.
float g_meter_snr_db = 0.0f;
bool g_meter_open = false;

bool hit(int32_t x, int32_t y, const Rect& r) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int cx(const Rect& r) { return r.x + r.w / 2; }
int cy(const Rect& r) { return r.y + r.h / 2; }

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void fitted_text(const char* value,int x,int y,int width,uint16_t color,int size,textdatum_t datum=middle_left){
  char shown[128]{};std::snprintf(shown,sizeof(shown),"%s",value?value:"");
  M5.Display.setTextSize(size);
  while(shown[0]&&M5.Display.textWidth(shown)>width){
    const size_t n=std::strlen(shown);if(n<=3){shown[0]='\0';break;}
    shown[n-4]='.';shown[n-3]='.';shown[n-2]='.';shown[n-1]='\0';
  }
  text(shown,x,y,color,size,datum);
}

void wrapped_detail(const char* value,int x,int y,int width,uint16_t color){
  const size_t cap=static_cast<size_t>(std::max(12,width/12));
  const size_t length=std::strlen(value);
  for(size_t offset=0;offset<length;offset+=cap){
    char line[80]{};const size_t n=std::min(std::min(cap,sizeof(line)-1),length-offset);
    std::memcpy(line,value+offset,n);text(line,x,y+static_cast<int>(offset/cap)*26,color,2,middle_left);
  }
}

void card(const Rect& r, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 10, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 10, border);
}

void button(const Rect& r, const char* label, bool selected = false,
            bool enabled = true, int size = 2) {
  if (enabled) focus_nav::note(r.x, r.y, r.w, r.h);   // keyboard focus stop
  const uint16_t border = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  fitted_text(label,cx(r),cy(r),std::max(1,r.w-16),enabled?TFT_WHITE:kMuted,size,middle_center);
}

Rect control(int col, int row) {
  return g_layout.control[row * 3 + col];
}

Rect list_row(int row) {
  return g_tab == Tab::scan ? g_layout.scan_rows[row]
       : g_tab == Tab::airports ? g_layout.airport_rows[row]
       : g_layout.activity_rows[row];
}

Rect setup_rect(int kind, int row) { return kind == 0 ? g_layout.setup_minus[row] : kind == 1 ? g_layout.setup_value[row] : g_layout.setup_plus[row]; }

double mhz(uint32_t hz) { return static_cast<double>(hz) / 1000000.0; }

uint16_t state_color() {
  switch (g_snapshot.scan_state) {
    case ScanState::receiving: return kGreen;
    case ScanState::settling: return kYellow;
    case ScanState::hang: return kAmber;
    case ScanState::held: return kAmber;
    case ScanState::scanning: return kCyan;
    case ScanState::off: return kMuted;
  }
  return kMuted;
}

void draw_header() {
  const auto& h = audio_header::layout();
  audio_header::draw_brand("AIRBAND");
  if (h.legacy) {
    M5.Display.drawFastVLine(350,18,58,kCyan);
    text("AIRBAND RX",390,42,TFT_WHITE,4,middle_left);
    text("118.000 - 136.975 MHz  AM",390,70,kMuted,1,middle_left);
  } else if (h.status.w > 120) {
    const int x=h.status.x+8,y=h.status.y+h.status.h/2;
    M5.Display.drawFastVLine(h.status.x,h.status.y+4,h.status.h-8,kCyan);
    text("AIRBAND RX",x,y-10,TFT_WHITE,h.status.w>280?3:2,middle_left);
    text("118.000 - 136.975 MHz  AM",x,y+12,kMuted,1,middle_left);
  }
  audio_header::draw_battery(g_snapshot.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(g_snapshot.sound_enabled);
  audio_header::draw_visualizer_button(g_snapshot.running);
  audio_header::draw_settings_button();
  if(g_layout.legacy)M5.Display.drawFastHLine(20,92,1240,kGreen);
  else M5.Display.drawFastHLine(g_layout.viewport.x+8,g_layout.content.y-1,g_layout.viewport.w-16,kGreen);
}

void draw_tabs() {
  static constexpr const char* labels[kTabCount] = {
      "LISTEN", "SCAN", "AIRPORTS", "ACTIVITY", "SETUP", "SCOPE"};
  for (int i = 0; i < kTabCount; ++i)
    button(g_layout.tab[i],labels[i],
           static_cast<int>(g_tab) == i);
}

void draw_scope_chips() {
  using waterfall_style::Screen;
  const auto chip = [](const Rect& r, const char* title, const char* value) {
    button(r, "", false, true, 1);
    text(title,cx(r),r.y+15,kCyan,g_layout.legacy?1:2);
    text(value, cx(r), r.y + 36, kGreen, 2);
  };
  chip(kScopePaletteChip, "PALETTE",
       waterfall_style::palette_name(waterfall_style::palette(Screen::airband)));
  chip(kScopeSpeedChip, "SPEED", waterfall_style::speed_name(waterfall_style::speed(Screen::airband)));
}

void format_mhz(char* out, size_t size, uint32_t hz) {
  std::snprintf(out, size, "%.3f", static_cast<double>(hz) / 1e6);
}

void draw_scope_axis() {
  M5.Display.fillRect(g_layout.scope_axis.x,g_layout.scope_axis.y,g_layout.scope_axis.w,g_layout.scope_axis.h,TFT_BLACK);
  if (g_scope_span_hz == 0) return;
  const uint32_t half = g_scope_span_hz / 2u;
  const uint32_t centre = g_snapshot.frequency_hz;
  char low[16], mid[16], high[16];
  format_mhz(low, sizeof(low), centre > half ? centre - half : 0u);
  format_mhz(mid, sizeof(mid), centre);
  format_mhz(high, sizeof(high), centre + half);
  const int y = g_layout.scope_axis.y + g_layout.scope_axis.h / 2;
  text(low,g_layout.scope_axis.x+4,y,TFT_LIGHTGREY,2,middle_left);
  text(mid,g_layout.scope_axis.x+g_layout.scope_axis.w/2,y,kCyan,2,middle_center);
  text(high,g_layout.scope_axis.x+g_layout.scope_axis.w-4,y,TFT_LIGHTGREY,2,middle_right);
}

void draw_scope_status() {
  if(g_layout.scope_status.w<=0||g_layout.scope_status.h<=0)return;
  M5.Display.fillRect(g_layout.scope_status.x,g_layout.scope_status.y,g_layout.scope_status.w,g_layout.scope_status.h,TFT_BLACK);
  char value[96];
  std::snprintf(value, sizeof(value), "SNR %.0f dB   SQL +%d   %s   FILTER %.0f kHz",
                static_cast<double>(g_snapshot.snr_db), static_cast<int>(g_snapshot.scan.squelch_db),
                g_snapshot.squelch_open ? "OPEN" : "CLOSED",
                static_cast<double>(g_snapshot.filter_hz) / 1000.0);
  text(value,g_layout.scope_status.x+6,g_layout.scope_status.y+g_layout.scope_status.h/2,g_snapshot.squelch_open?kGreen:TFT_LIGHTGREY,2,middle_left);
}

void scope_value_cell(const Rect& r, const char* label, const char* value, uint16_t colour) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, kMuted);
  text(label, cx(r), r.y + 11, kCyan, 1);
  text(value, cx(r), r.y + 33, colour, 3);
}

void draw_scope_controls() {
  using receiver_controls::Availability;
  using receiver_controls::Control;
  for(int i=0;i<g_layout.scope_control_count;++i){const auto&r=g_layout.scope_control[i];M5.Display.fillRect(r.x,r.y,r.w,r.h,TFT_BLACK);}
  char value[40];
  const auto agc = receiver_controls::item(Control::tuner_agc, g_snapshot.controls);
  const auto gain = receiver_controls::item(Control::rf_gain, g_snapshot.controls);
  const bool gain_ok = gain.availability == Availability::enabled;
  const bool agc_ok = agc.availability == Availability::enabled;
  const bool scanning=g_snapshot.scan_state!=ScanState::off;
  if(g_layout.legacy){
    std::snprintf(value,sizeof(value),"%.3f",static_cast<double>(g_snapshot.frequency_hz)/1e6);
    scope_value_cell(scope_value(0,g_layout.scope_control[0].y),"CHANNEL MHz",value,TFT_WHITE);
    std::snprintf(value,sizeof(value),"%lu kHz",static_cast<unsigned long>(g_scope_span_hz/1000u));
    scope_value_cell(scope_value(1,g_layout.scope_control[0].y),"SPAN",value,kGreen);
    if(!gain_ok)std::snprintf(value,sizeof(value),"N/A");
    else if(agc_ok&&agc.active)std::snprintf(value,sizeof(value),"AUTO");
    else std::snprintf(value,sizeof(value),"%.1f dB",double(gain.value)/10.0);
    scope_value_cell(scope_value(2,g_layout.scope_control[0].y),"GAIN",value,gain_ok?kGreen:kMuted);
    if(g_snapshot.scan.squelch_db==0)std::snprintf(value,sizeof(value),"OPEN");else std::snprintf(value,sizeof(value),"+%d dB",static_cast<int>(g_snapshot.scan.squelch_db));
    scope_value_cell(scope_value(0,g_layout.scope_control[6].y),g_snapshot.squelch_open?"SQUELCH  (open)":"SQUELCH",value,g_snapshot.squelch_open?kGreen:TFT_WHITE);
    std::snprintf(value,sizeof(value),"%.0f kHz",double(g_snapshot.filter_hz)/1000.0);
    scope_value_cell(scope_value(1,g_layout.scope_control[6].y),"FILTER",value,kYellow);
    button(g_layout.scope_control[0],"-",false,true,3);button(g_layout.scope_control[1],"+",false,true,3);
    button(g_layout.scope_control[2],"-",false,true,3);button(g_layout.scope_control[3],"+",false,true,3);
    button(g_layout.scope_control[4],"-",false,gain_ok,3);button(g_layout.scope_control[5],"+",false,gain_ok,3);
    button(g_layout.scope_control[6],"-",false,true,3);button(g_layout.scope_control[7],"+",false,true,3);
    button(g_layout.scope_control[8],"-",false,true,3);button(g_layout.scope_control[9],"+",false,true,3);
    button(g_layout.scope_control[10],!agc_ok?"AGC N/A":agc.active?"AGC ON":"AGC OFF",agc_ok&&agc.active,agc_ok,2);
    button(g_layout.scope_control[11],scanning?"STOP":"SCAN",scanning,true,2);
    button(g_layout.scope_control[12],"121.5",g_snapshot.frequency_hz==kGuardFrequencyHz,true,2);
  }else{
    static constexpr const char* labels[15]={"CH -","CH +","SPAN -","SPAN +","GAIN -","GAIN +","SQL -","SQL +","FILT -","FILT +","AGC","SCAN","GUARD","PAL","SPEED"};
    for(int i=0;i<15;++i)button(g_layout.scope_control[i],labels[i],(i==10&&agc.active)||(i==11&&scanning)||(i==12&&g_snapshot.frequency_hz==kGuardFrequencyHz),(i!=4&&i!=5)||gain_ok,2);
  }
}

// The tuned frequency is the scope's heading; it must follow every retune (tap, scanner, keys).
void draw_scope_title() {
  M5.Display.fillRect(g_layout.scope_title.x,g_layout.scope_title.y,g_layout.scope_title.w,g_layout.scope_title.h,TFT_BLACK);
  char value[128];
  if(g_layout.scope_status.h==0)std::snprintf(value,sizeof(value),"%.3f MHz AM   SNR %.0f dB   SPAN %lu kHz   FILTER %.0f kHz",double(g_snapshot.frequency_hz)/1e6,double(g_snapshot.snr_db),static_cast<unsigned long>(g_scope_span_hz/1000u),double(g_snapshot.filter_hz)/1000.0);
  else std::snprintf(value,sizeof(value),"%.3f MHz  AM",static_cast<double>(g_snapshot.frequency_hz)/1e6);
  text(value,g_layout.scope_title.x+8,g_layout.scope_title.y+g_layout.scope_title.h/2,TFT_WHITE,g_layout.scope_title.h<44?2:3,middle_left);
}

void draw_scope() {
  M5.Display.fillRect(g_layout.content.x,g_layout.content.y,g_layout.content.w,g_layout.content.h,TFT_BLACK);
  draw_scope_title();
  if(g_layout.scope_status.h>0)draw_scope_chips();
  M5.Display.drawRect(g_layout.scope_plot.x,g_layout.scope_plot.y,g_layout.scope_plot.w,g_layout.scope_plot.h,kCyan);
  M5.Display.drawRect(g_layout.scope_waterfall.x,g_layout.scope_waterfall.y,g_layout.scope_waterfall.w,g_layout.scope_waterfall.h,kCyan);
  g_scope_ceiling_valid = false;
  draw_scope_axis();
  draw_scope_status();
  draw_scope_controls();
  M5.Display.setScrollRect(g_layout.scope_waterfall.x+1,g_layout.scope_waterfall.y+1,g_layout.scope_waterfall.w-2,g_layout.scope_waterfall.h-2,TFT_BLACK);
}

void page_title(const char* title, const char* subtitle) {
  M5.Display.fillRect(g_layout.content.x,g_layout.content.y,g_layout.content.w,g_layout.content.h,TFT_BLACK);
  if(g_layout.legacy){text(title,32,126,TFT_WHITE,3,middle_left);text(subtitle,34,160,kMuted,2,middle_left);return;}
  const int x=g_layout.content.x+8;
  text(title,x,g_layout.content.y+24,TFT_WHITE,3,middle_left);
  text(subtitle,x,g_layout.content.y+58,kMuted,2,middle_left);
}

Rect meter_area() { return g_layout.legacy?Rect{36,296,496,62}:Rect{kNowCard.x+16,kNowCard.y+kNowCard.h-76,kNowCard.w-32,68}; }

void draw_signal_meter() {
  g_meter_snr_db = g_snapshot.snr_db;
  g_meter_open = g_snapshot.squelch_open;
  const Rect area=meter_area();
  M5.Display.fillRect(area.x,area.y,area.w,area.h,kPanel);
  const Rect meter=g_layout.legacy?Rect{48,304,468,24}:Rect{area.x+8,area.y+8,area.w-16,24};
  M5.Display.drawRoundRect(meter.x, meter.y, meter.w, meter.h, 6, kCyan);
  const float level = std::clamp(g_snapshot.snr_db / 30.0f, 0.0f, 1.0f);
  const int filled = static_cast<int>((meter.w - 4) * level);
  if (filled > 0)
    M5.Display.fillRect(meter.x + 2, meter.y + 3, filled, meter.h - 6,
                        g_snapshot.squelch_open ? kGreen : kYellow);
  char value[64];
  const int threshold_x = meter.x + 2 +
      static_cast<int>((meter.w - 4) * std::clamp(
          static_cast<float>(g_snapshot.scan.squelch_db) / 30.0f, 0.0f, 1.0f));
  M5.Display.drawFastVLine(threshold_x, meter.y - 4, meter.h + 8, kAmber);
  std::snprintf(value, sizeof(value), "SNR %.0f dB   SQL +%d dB   %s",
                static_cast<double>(g_snapshot.snr_db),
                static_cast<int>(g_snapshot.scan.squelch_db),
                g_snapshot.squelch_open ? "OPEN" : "CLOSED");
  text(value,meter.x,meter.y+42,g_snapshot.squelch_open?kGreen:kMuted,2,middle_left);
}

void draw_gain() {
  using receiver_controls::Availability;
  using receiver_controls::Control;
  card(kGainCard);
  const auto agc = receiver_controls::item(Control::tuner_agc, g_snapshot.controls);
  const auto gain = receiver_controls::item(Control::rf_gain, g_snapshot.controls);
  const auto rtl = receiver_controls::item(Control::rtl_agc, g_snapshot.controls);
  const bool agc_ok = agc.availability == Availability::enabled;
  const bool gain_ok = gain.availability == Availability::enabled;
  const bool rtl_ok = rtl.availability == Availability::enabled;
  const int size=2;
  button(kGainAuto,!agc_ok?(g_layout.legacy?"TUNER AGC N/A":"AGC N/A"):agc.active?(g_layout.legacy?"TUNER AGC ON":"AGC ON"):(g_layout.legacy?"TUNER AGC OFF":"AGC OFF"),agc_ok&&agc.active,agc_ok,size);
  button(kGainDown,"GAIN -",false,gain_ok,size);
  char value[32];
  if (!gain_ok)
    std::snprintf(value, sizeof(value), "RF GAIN N/A");
  else if (agc_ok && agc.active)
    std::snprintf(value, sizeof(value), "GAIN AUTO");
  else
    std::snprintf(value, sizeof(value), "%.1f dB", static_cast<double>(gain.value) / 10.0);
  M5.Display.drawRoundRect(kGainValue.x,kGainValue.y,kGainValue.w,kGainValue.h,8,kMuted);
  text(value,cx(kGainValue),cy(kGainValue),gain_ok?TFT_WHITE:kMuted,size);
  button(kGainUp,"GAIN +",false,gain_ok,size);
  button(kRtlAgc,!rtl_ok?"RTL N/A":rtl.active?"RTL AGC ON":"RTL AGC OFF",rtl_ok&&rtl.active,rtl_ok,size);
  if(g_layout.legacy){text("Shared receiver gain",1120,560,kMuted,1,middle_left);text("(same driver controls",1120,578,kMuted,1,middle_left);text("as AM / Shortwave)",1120,596,kMuted,1,middle_left);}
  else if(kGainCard.h>=80)text("Shared receiver gain also controls AM / Shortwave",kGainCard.x+kGainCard.w-12,kGainCard.y+kGainCard.h-10,kMuted,1,middle_right);
}

void draw_now_card() {
  card(kNowCard, state_color());
  char value[80];
  std::snprintf(value, sizeof(value), "%.3f", mhz(g_snapshot.frequency_hz));
  if(g_layout.legacy){
    text(value,48,166,TFT_WHITE,7,middle_left);text("MHz",48,222,kCyan,3,middle_left);
    text(g_snapshot.current_label[0]?g_snapshot.current_label:"AIRBAND",48,258,kMuted,2,middle_left);
    std::snprintf(value,sizeof(value),"%s   AM",state_name(g_snapshot.scan_state));text(value,516,255,state_color(),2,middle_right);draw_signal_meter();
  }else if(kNowCard.h<120){
    std::snprintf(value,sizeof(value),"%.3f MHz",mhz(g_snapshot.frequency_hz));text(value,kNowCard.x+12,kNowCard.y+12,TFT_WHITE,2,middle_left);
    std::snprintf(value,sizeof(value),"%s   SNR %.0f dB   %s",state_name(g_snapshot.scan_state),double(g_snapshot.snr_db),g_snapshot.squelch_open?"OPEN":"CLOSED");text(value,kNowCard.x+12,kNowCard.y+36,kGreen,2,middle_left);
    std::snprintf(value,sizeof(value),"%lu stops   %lu checked   bank %u",static_cast<unsigned long>(g_snapshot.stops),static_cast<unsigned long>(g_snapshot.channels_checked),static_cast<unsigned>(g_snapshot.bank_count));text(value,kNowCard.x+12,kNowCard.y+60,kMuted,2,middle_left);
  }else{
    const bool compact=kNowCard.w<600||kNowCard.h<240;
    text(value,kNowCard.x+20,kNowCard.y+(compact?44:56),TFT_WHITE,compact?4:7,middle_left);
    if(compact){
      std::snprintf(value,sizeof(value),"%s  %s",g_snapshot.current_label[0]?g_snapshot.current_label:"AIRBAND",state_name(g_snapshot.scan_state));
      text(value,kNowCard.x+20,kNowCard.y+82,kMuted,2,middle_left);
      draw_signal_meter();
    }else{
      text("MHz",kNowCard.x+20,kNowCard.y+112,kCyan,3,middle_left);
      text(g_snapshot.current_label[0]?g_snapshot.current_label:"AIRBAND",kNowCard.x+20,kNowCard.y+150,kMuted,2,middle_left);
      std::snprintf(value,sizeof(value),"%s   AM",state_name(g_snapshot.scan_state));text(value,kNowCard.x+kNowCard.w-20,kNowCard.y+150,state_color(),2,middle_right);draw_signal_meter();
    }
  }
}

void draw_controls() {
  card(kControlsCard);
  const bool scanning = g_snapshot.scan_state != ScanState::off;
  const bool held = g_snapshot.scan_state == ScanState::held;
  const int size=2;
  button(control(0,0),scanning?"STOP SCAN":"START SCAN",scanning,true,size);
  button(control(1,0),held?"RESUME":"HOLD",held,scanning,size);
  button(control(2,0),"SKIP",false,scanning,size);
  button(control(0,1),"FREQ -",false,true,size);
  button(control(1,1),"121.500 GUARD",g_snapshot.frequency_hz==kGuardFrequencyHz,true,size);
  button(control(2,1),"FREQ +",false,true,size);
  button(control(0,2),g_layout.legacy?source_name(g_snapshot.scan.source):g_snapshot.scan.source==ScanSource::airport_bank?"AIRPORTS":"FULL BAND",
         g_snapshot.scan.source == ScanSource::airport_bank);
  button(control(1,2),spacing_name(g_snapshot.scan.spacing),false,true,size);
  button(control(2, 2), g_snapshot.catalog_loaded ? "AVIATION DATA READY" : "NO AVIATION DATA",
         g_snapshot.catalog_loaded,true,size);
}

void draw_status() {
  if(kStatusCard.w<=0||kStatusCard.h<=0)return;
  card(kStatusCard);
  char value[112];
  std::snprintf(value, sizeof(value),
                "%lu stops   %lu checked   bank %u   guard %s   SQL +%d dB   hang %.1fs",
                static_cast<unsigned long>(g_snapshot.stops),
                static_cast<unsigned long>(g_snapshot.channels_checked),
                static_cast<unsigned>(g_snapshot.bank_count),
                g_snapshot.scan.priority_guard ? "ON" : "OFF",
                static_cast<int>(g_snapshot.scan.squelch_db),
                static_cast<double>(g_snapshot.scan.hang_ms) / 1000.0);
  if(kStatusCard.h>0)text(value,kStatusCard.x+(g_layout.legacy?20:16),kStatusCard.y+(g_layout.legacy?30:kStatusCard.h/3),TFT_WHITE,g_layout.legacy?2:kStatusCard.w<600?1:2,middle_left);
  if (!g_snapshot.location_configured)
    text("Receiver location not set: airport labels and airport-bank scanning are off.",
         kStatusCard.x+(g_layout.legacy?20:16),kStatusCard.y+(g_layout.legacy?70:kStatusCard.h*2/3),kAmber,g_layout.legacy?2:kStatusCard.w<600?1:2,middle_left);
  else if (!g_snapshot.catalog_loaded)
    text("Aviation data not loaded: manual tuning and full-band scan still work.",
         kStatusCard.x+(g_layout.legacy?20:16),kStatusCard.y+(g_layout.legacy?70:kStatusCard.h*2/3),kAmber,g_layout.legacy?2:kStatusCard.w<600?1:2,middle_left);
  else
    text("Airport bank uses the closest catalog entries (database, not RF-decoded).",
         kStatusCard.x+(g_layout.legacy?20:16),kStatusCard.y+(g_layout.legacy?70:kStatusCard.h*2/3),kGreen,g_layout.legacy?2:kStatusCard.w<600?1:2,middle_left);
}

void draw_listen() {
  M5.Display.fillRect(g_layout.content.x,g_layout.content.y,g_layout.content.w,g_layout.content.h,TFT_BLACK);
  draw_now_card();
  draw_controls();
  draw_status();
  draw_gain();
}

void draw_scan_status() {
  char value[96];
  card(g_layout.scan_status,state_color());
  std::snprintf(value, sizeof(value), "%s   target %.3f MHz",
                state_name(g_snapshot.scan_state),
                mhz(g_snapshot.frequency_hz));
  text(value,g_layout.legacy?44:g_layout.scan_status.x+16,g_layout.legacy?208:g_layout.scan_status.y+30,state_color(),g_layout.scan_status.h<80?2:3,middle_left);
  std::snprintf(value, sizeof(value), "%s   %s   SQL +%d dB",
                source_name(g_snapshot.scan.source), spacing_name(g_snapshot.scan.spacing),
                static_cast<int>(g_snapshot.scan.squelch_db));
  text(value,g_layout.legacy?44:g_layout.scan_status.x+16,g_layout.legacy?252:g_layout.scan_status.y+g_layout.scan_status.h-24,kMuted,2,middle_left);
  button(g_layout.scan_toggle,
         g_snapshot.scan_state == ScanState::off ? "START SCAN" : "STOP SCAN",
         g_snapshot.scan_state != ScanState::off);
}

void draw_scan_bank() {
  char value[96];
  M5.Display.fillRect(g_layout.scan_rows[0].x,g_layout.scan_rows[0].y,g_layout.scan_rows[0].w,g_layout.content.y+g_layout.content.h-g_layout.scan_rows[0].y,TFT_BLACK);
  text("SCAN BANK",g_layout.legacy?30:g_layout.content.x+8,g_layout.legacy?308:g_layout.scan_rows[0].y-14,kCyan,2,middle_left);
  const size_t page_size=static_cast<size_t>(g_layout.scan_row_count);
  const size_t pages=std::max<size_t>(1,(g_snapshot.bank_count+page_size-1)/page_size);
  if(g_bank_page>=pages)g_bank_page=pages-1;
  const size_t first=g_bank_page*page_size;
  const size_t rows=std::min(page_size,g_snapshot.bank_count-first);
  for (size_t i = 0; i < rows; ++i) {
    const Rect r=g_layout.scan_rows[i];
    const auto& entry=g_snapshot.bank[first+i];
    std::snprintf(value,sizeof(value),"%2u   %9.3f MHz   %-40.40s",static_cast<unsigned>(first+i+1),mhz(entry.frequency_hz),entry.label);
    button(r,value,entry.frequency_hz==g_snapshot.frequency_hz,true,2);
  }
  button(g_layout.scan_previous,"<",false,g_bank_page>0);
  button(g_layout.scan_page,"PAGE",false,true,1);
  button(g_layout.scan_next,">",false,g_bank_page+1<pages);
  if (rows == 0)
    text("No airport bank loaded. Switch to FULL BAND or reload the FAA data pack.",
         640, 430, kAmber, 2);
}

void draw_scan() {
  page_title("SMART SCAN", "Fast memory-bank scan first; full-band scan remains available.");
  draw_scan_status();
  draw_scan_bank();
}

void draw_airports() {
  page_title("AIRPORT / ATC CHANNELS",
             "Offline aviation catalog. Nearby rows are database context, not RF-decoded identity.");
  button(g_layout.airport_reload,
         g_snapshot.location_configured ? "RELOAD DATA" : "SET LOCATION");
  char radius_label[32];
  if (g_snapshot.scan.radius_nm == 0)
    std::snprintf(radius_label, sizeof(radius_label), "RADIUS: ANY");
  else
    std::snprintf(radius_label, sizeof(radius_label), "RADIUS: %u nm",
                  static_cast<unsigned>(g_snapshot.scan.radius_nm));
  button(g_layout.airport_radius,radius_label,false,g_snapshot.location_configured);
  char value[128];
  if (!g_snapshot.location_configured) {
    const Rect body{g_layout.content.x+8,g_layout.airport_radius.y+g_layout.airport_radius.h+12,g_layout.content.w-16,g_layout.content.h-120};card(body);
    text("RECEIVER LOCATION NOT SET",cx(body),body.y+body.h/4,kAmber,3);
    text("Set one OrcSDR receiver location for Airband, ADS-B, and OrcMaps.",cx(body),body.y+body.h/2,TFT_WHITE,2);
    text("Airport-bank scanning disabled; manual tuning and full-band scan remain available.",cx(body),body.y+3*body.h/4,kMuted,2);
    return;
  }
  if (!g_snapshot.catalog_loaded || g_snapshot.catalog_count == 0) {
    const Rect body{g_layout.content.x+8,g_layout.airport_radius.y+g_layout.airport_radius.h+12,g_layout.content.w-16,g_layout.content.h-120};card(body);
    switch (g_snapshot.load_result) {
      case LoadResult::no_matching_entries:
        text("NO AVIATION DATA NEAR THIS LOCATION",cx(body),body.y+body.h/3,kAmber,3);
        text("Nothing in the catalog is inside the radius shown above.",cx(body),body.y+body.h/2,TFT_WHITE,2);
        text("Increase radius, or use manual tuning / full-band scan.",cx(body),body.y+2*body.h/3,kMuted,2);
        break;
      case LoadResult::bad_header:
      case LoadResult::unsupported_rows:
        text("AVIATION DATA FORMAT NOT SUPPORTED",cx(body),body.y+body.h/3,kAmber,3);
        text("Installed file is not an ORCAIR2 airport catalog.",cx(body),body.y+body.h/2,TFT_WHITE,2);
        text("See docs/airband/README.md for the catalog path.",cx(body),body.y+2*body.h/3,kMuted,2);
        break;
      default:
        text("AVIATION DATA NOT INSTALLED",cx(body),body.y+body.h/3,kAmber,3);
        text("Expected: /orcsdr/data/aviation.idx (ORCAIR2).",cx(body),body.y+body.h/2,TFT_WHITE,2);
        text("Manual tuning and full-band scan remain available.",cx(body),body.y+2*body.h/3,kMuted,2);
        break;
    }
    return;
  }
  const size_t page_size=static_cast<size_t>(g_layout.airport_row_count);
  const size_t pages=std::max<size_t>(1,(g_snapshot.catalog_count+page_size-1)/page_size);
  if(g_airport_page>=pages)g_airport_page=pages-1;
  const size_t first=g_airport_page*page_size;
  const size_t rows=std::min(page_size,g_snapshot.catalog_count-first);
  for (size_t i = 0; i < rows; ++i) {
    const CatalogEntry& entry=g_snapshot.catalog[first+i];
    std::snprintf(value, sizeof(value),
                  "%9.3f MHz   %-9s   %5.1f nm   %-9s   %-34.34s",
                  mhz(entry.frequency_hz), service_name(entry.service),
                  static_cast<double>(entry.distance_nm),
                  source_class_name(entry.source_class), entry.label);
    button(g_layout.airport_rows[i],value,entry.frequency_hz==g_snapshot.frequency_hz,true,2);
  }
  button(g_layout.airport_previous,"<",false,g_airport_page>0);
  char page[32];std::snprintf(page,sizeof(page),"PAGE %u/%u",unsigned(g_airport_page+1),unsigned(pages));button(g_layout.airport_page,page,false,true,1);
  button(g_layout.airport_next,">",false,g_airport_page+1<pages);
}

void format_age(uint32_t now_ms, const Activity& item, char* out, size_t size) {
  const uint32_t ended = item.start_ms + item.duration_ms;
  const uint32_t seconds = (now_ms - ended) / 1000u;
  if (seconds < 60u)
    std::snprintf(out, size, "%lus ago", static_cast<unsigned long>(seconds));
  else
    std::snprintf(out, size, "%lum ago", static_cast<unsigned long>(seconds / 60u));
}

void draw_activity() {
  page_title("ACTIVITY", "Bounded local log of transmissions that opened the airband scanner.");
  button(g_layout.activity_clear,"CLEAR LOG",false,g_snapshot.activity_count!=0);
  if (g_snapshot.activity_count == 0) {
    const Rect body{g_layout.content.x+8,g_layout.activity_clear.y+g_layout.activity_clear.h+12,g_layout.content.w-16,g_layout.content.h-120};card(body);
    text("NO AIRBAND ACTIVITY LOGGED YET",cx(body),body.y+body.h/3,TFT_WHITE,3);
    text("Completed transmissions show frequency, duration, peak level, and station label.",cx(body),body.y+2*body.h/3,kMuted,2);
    return;
  }
  char value[128];
  const size_t page_size=static_cast<size_t>(g_layout.activity_row_count);
  const size_t pages=std::max<size_t>(1,(g_snapshot.activity_count+page_size-1)/page_size);
  if(g_activity_page>=pages)g_activity_page=pages-1;
  const size_t first=g_activity_page*page_size;
  const size_t rows=std::min(page_size,g_snapshot.activity_count-first);
  for (size_t i = 0; i < rows; ++i) {
    const Activity& item=g_snapshot.activity[first+i];
    char age[20];
    format_age(g_snapshot.now_ms, item, age, sizeof(age));
    std::snprintf(value, sizeof(value),
                  "%9.3f MHz   %5.1fs   +%2.0f dB   %-34.34s   %s",
                  mhz(item.frequency_hz),
                  static_cast<double>(item.duration_ms) / 1000.0,
                  static_cast<double>(item.peak_snr_db), item.label, age);
    button(g_layout.activity_rows[i],value,item.frequency_hz==g_snapshot.frequency_hz,true,2);
  }
  button(g_layout.activity_previous,"<",false,g_activity_page>0);
  char page[32];std::snprintf(page,sizeof(page),"PAGE %u/%u",unsigned(g_activity_page+1),unsigned(pages));button(g_layout.activity_page,page,false,true,1);
  button(g_layout.activity_next,">",false,g_activity_page+1<pages);
}

void draw_activity_rows(){
  if(g_snapshot.activity_count==0)return;
  const size_t page_size=static_cast<size_t>(g_layout.activity_row_count);
  const size_t first=g_activity_page*page_size;
  const size_t rows=std::min(page_size,g_snapshot.activity_count-first);
  char value[128];
  for(size_t i=0;i<rows;++i){
    const auto&item=g_snapshot.activity[first+i];char age[20];format_age(g_snapshot.now_ms,item,age,sizeof(age));
    std::snprintf(value,sizeof(value),"%9.3f MHz   %5.1fs   +%2.0f dB   %-34.34s   %s",mhz(item.frequency_hz),double(item.duration_ms)/1000.0,double(item.peak_snr_db),item.label,age);
    const Rect r=g_layout.activity_rows[i];button(r,value,item.frequency_hz==g_snapshot.frequency_hz,true,2);
  }
}

void setup_value(int row, char* out, size_t size) {
  switch (row) {
    case 0: std::snprintf(out, size, "%s", spacing_name(g_snapshot.scan.spacing)); break;
    case 1: std::snprintf(out, size, "%s", source_name(g_snapshot.scan.source)); break;
    case 2:
      if (g_snapshot.scan.squelch_db == 0)
        std::snprintf(out, size, "OPEN");
      else
        std::snprintf(out, size, "+%d dB", static_cast<int>(g_snapshot.scan.squelch_db));
      break;
    case 3: std::snprintf(out, size, "%u ms", static_cast<unsigned>(g_snapshot.scan.settle_ms)); break;
    case 4:
      std::snprintf(out, size, "%.1f s", static_cast<double>(g_snapshot.scan.hang_ms) / 1000.0);
      break;
    case 5:
      std::snprintf(out, size, "%s", g_snapshot.scan.priority_guard ? "ON" : "OFF");
      break;
    case 6:
      std::snprintf(out, size, "%s", g_snapshot.catalog_loaded ? "LOADED" : "MISSING");
      break;
    default: out[0] = '\0'; break;
  }
}

void draw_setup() {
  M5.Display.fillRect(g_layout.content.x,g_layout.content.y,g_layout.content.w,g_layout.content.h,TFT_BLACK);
  static constexpr const char* labels[kSetupRows] = {
      "CHANNEL SPACING", "SCAN SOURCE", "AUDIO / SCAN SQUELCH", "SCAN SETTLE",
      "REPLY HANG", "121.500 PRIORITY", "OFFLINE AVIATION DATA"};
  static constexpr const char* help[kSetupRows] = {
      "25 kHz or 8.33 kHz tuning plan",
      "Closest airport frequencies or the complete civil voice band",
      "Open audio and stop scan when the carrier is this far above the noise floor",
      "Time allowed after each tuner move before evaluating activity",
      "Wait this long for a reply before resuming the scan",
      "Periodically check the civil emergency / guard frequency",
      "Global/local catalog; database context is never RF identity"};

  for (int local = 0; local < g_layout.setup_visible_count; ++local) {
    const int row=g_layout.setup_first+g_setup_page*g_layout.setup_visible_count+local;
    if(row>=kSetupRows)break;
    const Rect rr=g_layout.setup_row[local];const int y=rr.y;
    M5.Display.fillRoundRect(rr.x,rr.y,rr.w,rr.h,8,kPanel);
    const int label_x=g_layout.legacy?42:rr.x+12;
    const int label_y=g_layout.legacy?y+20:g_layout.portrait?y+24:y+rr.h/3;
    const int help_y=g_layout.legacy?y+47:g_layout.portrait?y+50:y+2*rr.h/3;
    const int text_w=g_layout.legacy?720:g_layout.portrait?rr.w-24:g_layout.setup_minus[local].x-label_x-12;
    fitted_text(labels[row],label_x,label_y,text_w,TFT_WHITE,g_layout.legacy?3:2,middle_left);
    fitted_text(help[row],label_x,help_y,text_w,kMuted,g_layout.legacy?1:2,middle_left);
    char value[32];
    setup_value(row, value, sizeof(value));
    const bool cycle = row == 0 || row == 1 || row == 3 || row == 5 || row == 6;
    if (!cycle) button(setup_rect(0,row), "-");
    button(setup_rect(1,row), row == 6 && !g_snapshot.catalog_loaded
                                           ? "RELOAD DATA" : value,
           row == 5 && g_snapshot.scan.priority_guard);
    if (!cycle) button(setup_rect(2,row), "+");
  }
  if(g_layout.setup_page_count>1){button(g_layout.setup_previous,"<",false,g_setup_page>0);button(g_layout.setup_next,">",false,g_setup_page+1<g_layout.setup_page_count);}
}

void draw_detail_overlay();

void draw_page() {
  switch (g_tab) {
    case Tab::listen: draw_listen(); break;
    case Tab::scan: draw_scan(); break;
    case Tab::airports: draw_airports(); break;
    case Tab::activity: draw_activity(); break;
    case Tab::setup: draw_setup(); break;
    case Tab::scope: draw_scope(); break;
  }
  draw_detail_overlay();
}

void draw_detail_overlay(){
  if(g_detail_kind==DetailKind::none)return;
  focus_nav::clear();
  const Rect box=g_layout.detail_overlay;
  card(box,kYellow);
  button(g_layout.detail_close,"X",false,true,2);
  const int x=box.x+16,line=box.y+54,step=30,max_width=box.w-32;
  char text_value[120];
  if(g_detail_kind==DetailKind::catalog&&g_detail_index<g_snapshot.catalog_count){
    const auto&e=g_snapshot.catalog[g_detail_index];
    std::snprintf(text_value,sizeof(text_value),"%.3f MHz   %s",mhz(e.frequency_hz),service_name(e.service));text(text_value,x,line,TFT_WHITE,2,middle_left);
    wrapped_detail(e.airport_name,x,line+step,max_width,kCyan);
    std::snprintf(text_value,sizeof(text_value),"%s  %s  callsign %s",e.country,e.airport_ident,e.callsign);wrapped_detail(text_value,x,line+2*step,max_width,TFT_WHITE);
    std::snprintf(text_value,sizeof(text_value),"%s  %.1f nm  source %s",source_class_name(e.source_class),double(e.distance_nm),e.source);wrapped_detail(text_value,x,line+3*step,max_width,kMuted);
    std::snprintf(text_value,sizeof(text_value),"Location E7: %ld, %ld",static_cast<long>(e.latitude_e7),static_cast<long>(e.longitude_e7));text(text_value,x,line+4*step,kMuted,2,middle_left);
    wrapped_detail(e.label,x,line+5*step,max_width,TFT_WHITE);
  }else if(g_detail_kind==DetailKind::bank&&g_detail_index<g_snapshot.bank_count){
    const auto&e=g_snapshot.bank[g_detail_index];std::snprintf(text_value,sizeof(text_value),"%.3f MHz",mhz(e.frequency_hz));text(text_value,x,line,TFT_WHITE,2,middle_left);
    wrapped_detail(e.label,x,line+step,max_width,kCyan);
    std::snprintf(text_value,sizeof(text_value),"Airport distance: %.1f nm",double(e.distance_nm));text(text_value,x,line+2*step,kMuted,2,middle_left);
  }else if(g_detail_kind==DetailKind::activity&&g_detail_index<g_snapshot.activity_count){
    const auto&e=g_snapshot.activity[g_detail_index];std::snprintf(text_value,sizeof(text_value),"%.3f MHz   start %lu ms",mhz(e.frequency_hz),static_cast<unsigned long>(e.start_ms));text(text_value,x,line,TFT_WHITE,2,middle_left);
    std::snprintf(text_value,sizeof(text_value),"Duration %.1f s  Peak SNR %.1f dB",double(e.duration_ms)/1000.0,double(e.peak_snr_db));text(text_value,x,line+step,kGreen,2,middle_left);
    wrapped_detail(e.label,x,line+2*step,max_width,kCyan);
  }
}

Action tab_touch(int32_t x,int32_t y) {
  for(int i=0;i<kTabCount;++i) if(hit(x,y,g_layout.tab[i])) {set_tab(static_cast<Tab>(i));break;}
  draw_tabs();
  draw_page();
  return {};
}

Action scope_touch(int32_t x, int32_t y) {
  using waterfall_style::Screen;
  if(!g_layout.legacy){
    for(int i=0;i<15;++i)if(hit(x,y,g_layout.scope_control[i])){
      switch(i){case 0:return{ActionKind::tune_down};case 1:return{ActionKind::tune_up};case 2:return{ActionKind::span_down};case 3:return{ActionKind::span_up};case 4:return{ActionKind::gain_down};case 5:return{ActionKind::gain_up};case 6:return{ActionKind::squelch_down};case 7:return{ActionKind::squelch_up};case 8:return{ActionKind::filter_down};case 9:return{ActionKind::filter_up};case 10:return{ActionKind::tuner_agc_toggle};case 11:return{ActionKind::scan_toggle};case 12:return{ActionKind::tune_guard};case 13:waterfall_style::next_palette(Screen::airband);draw_scope_chips();return{};case 14:waterfall_style::next_speed(Screen::airband);draw_scope_chips();return{};}
    }
    const auto&p=g_layout.scope_plot;
    if(g_scope_span_hz&&x>=p.x&&x<p.x+p.w&&y>=p.y&&y<g_layout.scope_waterfall.y+g_layout.scope_waterfall.h){const int64_t offset=int64_t(x-(p.x+p.w/2))*g_scope_span_hz/p.w;const int64_t target=int64_t(g_snapshot.frequency_hz)+offset;if(target>0)return{ActionKind::tune_to,static_cast<int32_t>(target)};}
    return{};
  }
  const int a = kScopeRowAY, b = kScopeRowBY;
  if (hit(x, y, scope_minus(0, a))) return {ActionKind::tune_down};
  if (hit(x, y, scope_plus(0, a))) return {ActionKind::tune_up};
  if (hit(x, y, scope_minus(1, a))) return {ActionKind::span_down};
  if (hit(x, y, scope_plus(1, a))) return {ActionKind::span_up};
  if (hit(x, y, scope_minus(2, a))) return {ActionKind::gain_down};
  if (hit(x, y, scope_plus(2, a))) return {ActionKind::gain_up};
  if (hit(x, y, scope_minus(0, b))) return {ActionKind::squelch_down};
  if (hit(x, y, scope_plus(0, b))) return {ActionKind::squelch_up};
  if (hit(x, y, scope_minus(1, b))) return {ActionKind::filter_down};
  if (hit(x, y, scope_plus(1, b))) return {ActionKind::filter_up};
  if (hit(x, y, kScopeAgc)) return {ActionKind::tuner_agc_toggle};
  if (hit(x, y, kScopeScan)) return {ActionKind::scan_toggle};
  if (hit(x, y, kScopeGuard)) return {ActionKind::tune_guard};
  if (hit(x, y, kScopePaletteChip)) {
    waterfall_style::next_palette(Screen::airband);
    draw_scope_chips();
    return {};
  }
  if (hit(x, y, kScopeSpeedChip)) {
    waterfall_style::next_speed(Screen::airband);
    draw_scope_chips();
    return {};
  }
  if (g_scope_span_hz != 0 && x >= kScopeX && x < kScopeX + kScopeW && y >= kScopeSpecY &&
      y < kScopeWfY + kScopeWfH) {
    const int64_t offset = static_cast<int64_t>(x - (kScopeX + kScopeW / 2)) * g_scope_span_hz / kScopeW;
    const int64_t target = static_cast<int64_t>(g_snapshot.frequency_hz) + offset;
    if (target > 0) return {ActionKind::tune_to, static_cast<int32_t>(target)};
  }
  return {};
}

Action listen_touch(int32_t x, int32_t y) {
  if (hit(x, y, control(0, 0))) return {ActionKind::scan_toggle};
  if (hit(x, y, control(1, 0)))
    return g_snapshot.scan_state == ScanState::off ? Action{}
                                                   : Action{ActionKind::hold_toggle};
  if (hit(x, y, control(2, 0)))
    return g_snapshot.scan_state == ScanState::off ? Action{} : Action{ActionKind::skip};
  if (hit(x, y, control(0, 1))) return {ActionKind::tune_down};
  if (hit(x, y, control(1, 1))) return {ActionKind::tune_guard};
  if (hit(x, y, control(2, 1))) return {ActionKind::tune_up};
  if (hit(x, y, control(0, 2))) return {ActionKind::source_cycle};
  if (hit(x, y, control(1, 2))) return {ActionKind::spacing_cycle};
  if (hit(x, y, control(2, 2))) return {ActionKind::reload_catalog};
  using receiver_controls::Availability;
  using receiver_controls::Control;
  const bool gain_ok = receiver_controls::item(Control::rf_gain, g_snapshot.controls)
                           .availability == Availability::enabled;
  if (hit(x,y,kGainDown)&&gain_ok)return{ActionKind::gain_down};
  if (hit(x,y,kGainUp)&&gain_ok)return{ActionKind::gain_up};
  if (hit(x, y, kGainAuto) &&
      receiver_controls::item(Control::tuner_agc, g_snapshot.controls).availability ==
          Availability::enabled)
    return {ActionKind::tuner_agc_toggle};
  if (hit(x, y, kRtlAgc) &&
      receiver_controls::item(Control::rtl_agc, g_snapshot.controls).availability ==
          Availability::enabled)
    return {ActionKind::rtl_agc_toggle};
  return {};
}

Action scan_touch(int32_t x, int32_t y) {
  if(hit(x,y,g_layout.scan_toggle))return{ActionKind::scan_toggle};
  const size_t page_size=static_cast<size_t>(g_layout.scan_row_count),pages=std::max<size_t>(1,(g_snapshot.bank_count+page_size-1)/page_size);
  if(hit(x,y,g_layout.scan_previous)){if(g_bank_page) --g_bank_page;draw_scan_bank();return{};}
  if(hit(x,y,g_layout.scan_next)){if(g_bank_page+1<pages)++g_bank_page;draw_scan_bank();return{};}
  for(size_t i=0;i<std::min(page_size,g_snapshot.bank_count-g_bank_page*page_size);++i)
    if(hit(x,y,g_layout.scan_rows[i])){g_detail_kind=DetailKind::bank;g_detail_index=g_bank_page*page_size+i;draw_detail_overlay();return{ActionKind::tune_catalog,static_cast<int32_t>(g_detail_index)};}
  return {};
}

Action airports_touch(int32_t x, int32_t y) {
  const size_t page_size=static_cast<size_t>(g_layout.airport_row_count),pages=std::max<size_t>(1,(g_snapshot.catalog_count+page_size-1)/page_size);
  if(hit(x,y,g_layout.airport_previous)){if(g_airport_page)--g_airport_page;draw_page();return{};}
  if(hit(x,y,g_layout.airport_next)){if(g_airport_page+1<pages)++g_airport_page;draw_page();return{};}
  if (hit(x, y, g_layout.airport_radius) && g_snapshot.location_configured)
    return {ActionKind::radius_cycle};
  if (hit(x, y, g_layout.airport_reload))
    return g_snapshot.location_configured ? Action{ActionKind::reload_catalog}
                                          : Action{ActionKind::open_location_settings};
  for(size_t i=0;i<std::min(page_size,g_snapshot.catalog_count-g_airport_page*page_size);++i)
    if(hit(x,y,g_layout.airport_rows[i])){g_detail_kind=DetailKind::catalog;g_detail_index=g_airport_page*page_size+i;draw_detail_overlay();return{ActionKind::tune_catalog,static_cast<int32_t>(g_detail_index)};}
  return {};
}

Action activity_touch(int32_t x, int32_t y) {
  const size_t page_size=static_cast<size_t>(g_layout.activity_row_count),pages=std::max<size_t>(1,(g_snapshot.activity_count+page_size-1)/page_size);
  if(hit(x,y,g_layout.activity_previous)){if(g_activity_page)--g_activity_page;draw_page();return{};}
  if(hit(x,y,g_layout.activity_next)){if(g_activity_page+1<pages)++g_activity_page;draw_page();return{};}
  if (hit(x, y, g_layout.activity_clear) && g_snapshot.activity_count)
    return {ActionKind::clear_activity};
  for(size_t i=0;i<std::min(page_size,g_snapshot.activity_count-g_activity_page*page_size);++i)
    if(hit(x,y,g_layout.activity_rows[i])){g_detail_kind=DetailKind::activity;g_detail_index=g_activity_page*page_size+i;draw_detail_overlay();return{ActionKind::tune_catalog,-static_cast<int32_t>(g_detail_index)-1};}
  return {};
}

Action setup_touch(int32_t x, int32_t y) {
  if(g_layout.setup_page_count>1){
    if(hit(x,y,g_layout.setup_previous)){if(g_setup_page)--g_setup_page;draw_page();return{};}
    if(hit(x,y,g_layout.setup_next)){if(g_setup_page+1<g_layout.setup_page_count)++g_setup_page;draw_page();return{};}
  }
  for (int row = 0; row < kSetupRows; ++row) {
    const int local=row-g_setup_page*g_layout.setup_visible_count;
    if(local<0||local>=g_layout.setup_visible_count)continue;
    if (hit(x, y, setup_rect(1,local))) {
      switch (row) {
        case 0: return {ActionKind::spacing_cycle};
        case 1: return {ActionKind::source_cycle};
        case 3: return {ActionKind::settle_cycle};
        case 5: return {ActionKind::priority_toggle};
        case 6: return {ActionKind::reload_catalog};
        default: break;
      }
    }
    if (hit(x, y, setup_rect(0,local))) {
      if (row == 2) return {ActionKind::squelch_down};
      if (row == 4) return {ActionKind::hang_down};
    }
    if (hit(x, y, setup_rect(2,local))) {
      if (row == 2) return {ActionKind::squelch_up};
      if (row == 4) return {ActionKind::hang_up};
    }
  }
  return {};
}

void action_feedback(int32_t x,int32_t y){
  focus_nav::focus_at(x,y);
  for(size_t i=0;i<focus_nav::count();++i){focus_nav::Rect r{};if(!focus_nav::get(i,&r))continue;
    if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h){M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,8,kYellow);return;}}
}

}  // namespace

void dashboard_enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_snapshot.catalog_count=static_cast<uint8_t>(std::min<size_t>(g_snapshot.catalog_count,Catalog::kCapacity));
  g_snapshot.bank_count=static_cast<uint8_t>(std::min<size_t>(g_snapshot.bank_count,kBankCapacity));
  g_snapshot.activity_count=static_cast<uint8_t>(std::min<size_t>(g_snapshot.activity_count,kActivityCapacity));
  g_active = true;
  g_bank_page=g_airport_page=g_activity_page=0;g_setup_page=0;g_detail_kind=DetailKind::none;
  set_tab(Tab::listen);
  dashboard_draw();
}

bool dashboard_set_viewport(layout::Rect viewport) {
  const auto next=layout::make_layout(viewport);
  if(!next.valid||!audio_header::set_viewport(viewport))return false;
  g_layout=next;
  if(g_setup_page>=g_layout.setup_page_count)g_setup_page=static_cast<uint8_t>(g_layout.setup_page_count-1);
  focus_nav::set_viewport(1280,720);
  g_scope_levels_valid=false;g_scope_ceiling_valid=false;
  if(g_active)dashboard_draw();
  return true;
}
layout::Rect dashboard_viewport(){return g_layout.viewport;}

void dashboard_leave() {
  if (g_active) set_tab(Tab::listen);   // restores the shared span if the scope was showing
  g_active = false;
}

void dashboard_draw() {
  if (!g_active) return;
  M5.Display.fillRect(g_layout.viewport.x,g_layout.viewport.y,g_layout.viewport.w,g_layout.viewport.h,TFT_BLACK);
  focus_nav::clear();
  draw_header();
  draw_tabs();
  draw_page();
}

namespace {

uint32_t g_last_meter_ms = 0;
uint32_t g_last_status_ms = 0;

bool controls_differ(const receiver_controls::State& a, const receiver_controls::State& b) {
  return a.tuner_agc != b.tuner_agc || a.rtl_agc != b.rtl_agc ||
         a.gain_tenth_db != b.gain_tenth_db ||
         a.capabilities.rf_gain != b.capabilities.rf_gain ||
         a.capabilities.tuner_agc != b.capabilities.tuner_agc ||
         a.capabilities.rtl_agc != b.capabilities.rtl_agc;
}

}  // namespace

void dashboard_update(const Snapshot& snapshot) {
  if (!g_active) return;
  const Snapshot& old = g_snapshot;
  const bool header_changed =
      snapshot.sound_enabled != old.sound_enabled ||
      snapshot.battery_percent != old.battery_percent ||
      snapshot.running != old.running;
  const bool frequency_changed = snapshot.frequency_hz != old.frequency_hz;
  const bool state_changed = snapshot.scan_state != old.scan_state;
  const bool settings_changed =
      snapshot.scan.squelch_db != old.scan.squelch_db ||
      snapshot.scan.spacing != old.scan.spacing ||
      snapshot.scan.source != old.scan.source ||
      snapshot.scan.hang_ms != old.scan.hang_ms ||
      snapshot.scan.settle_ms != old.scan.settle_ms ||
      snapshot.scan.priority_guard != old.scan.priority_guard ||
      snapshot.scan.radius_nm != old.scan.radius_nm;
  const uint8_t catalog_count=static_cast<uint8_t>(std::min<size_t>(snapshot.catalog_count,Catalog::kCapacity));
  const uint8_t bank_count=static_cast<uint8_t>(std::min<size_t>(snapshot.bank_count,kBankCapacity));
  const uint8_t activity_count=static_cast<uint8_t>(std::min<size_t>(snapshot.activity_count,kActivityCapacity));
  const bool data_changed =
      snapshot.catalog_loaded != old.catalog_loaded ||
      catalog_count != old.catalog_count ||
      snapshot.load_result != old.load_result ||
      snapshot.location_configured != old.location_configured ||
      bank_count != old.bank_count ||
      activity_count != old.activity_count ||
      std::memcmp(snapshot.catalog,old.catalog,sizeof(CatalogEntry)*catalog_count)!=0 ||
      std::memcmp(snapshot.bank,old.bank,sizeof(BankEntry)*bank_count)!=0 ||
      std::memcmp(snapshot.activity,old.activity,sizeof(Activity)*activity_count)!=0;
  const bool label_changed =
      std::strncmp(snapshot.current_label, old.current_label, sizeof(snapshot.current_label)) != 0;
  const bool gain_changed = controls_differ(snapshot.controls, old.controls);
  const bool live_changed = snapshot.squelch_open != g_meter_open ||
                            std::fabs(snapshot.snr_db - g_meter_snr_db) >= 2.0f;
  const bool counters_changed = snapshot.stops != old.stops ||
                                snapshot.channels_checked != old.channels_checked;
  const uint32_t now = snapshot.now_ms;
  const uint32_t old_stops = old.stops;
  // Compared here, before g_snapshot (which `old` refers to) is overwritten with the new snapshot.
  const bool filter_changed = snapshot.filter_hz != old.filter_hz;
  const bool squelch_state_changed = snapshot.squelch_open != old.squelch_open;
  g_snapshot = snapshot;
  g_snapshot.catalog_count=catalog_count;g_snapshot.bank_count=bank_count;g_snapshot.activity_count=activity_count;
  if((g_detail_kind==DetailKind::catalog&&g_detail_index>=catalog_count)||
     (g_detail_kind==DetailKind::bank&&g_detail_index>=bank_count)||
     (g_detail_kind==DetailKind::activity&&g_detail_index>=activity_count))g_detail_kind=DetailKind::none;

  M5.Display.startWrite();
  if (header_changed) draw_header();
  switch (g_tab) {
    case Tab::listen:
      // Repaint only the regions whose data changed. The level meter alone is throttled
      // because it moves constantly on a noisy channel.
      if (frequency_changed || state_changed || label_changed) {
        draw_now_card();
        g_last_meter_ms = now;
      } else if (live_changed && now - g_last_meter_ms >= 150u) {
        draw_signal_meter();
        g_last_meter_ms = now;
      }
      if (frequency_changed || state_changed || settings_changed || data_changed)
        draw_controls();
      if (settings_changed || data_changed || snapshot.stops != old_stops ||
          (counters_changed && now - g_last_status_ms >= 1000u)) {
        draw_status();
        g_last_status_ms = now;
      }
      if (gain_changed) draw_gain();
      break;
    case Tab::scan:
      if (frequency_changed || state_changed || settings_changed) draw_scan_status();
      if (frequency_changed || data_changed) draw_scan_bank();
      break;
    case Tab::activity:
      if(data_changed){
        draw_page();
        g_last_activity_redraw_ms = now;
      }else if(g_detail_kind==DetailKind::none&&now-g_last_activity_redraw_ms>=5000u){
        draw_activity_rows();g_last_activity_redraw_ms=now;
      }
      break;
    case Tab::airports:
    case Tab::setup:
      // Static pages repaint only when what they show changes, never for the live level.
      if(settings_changed||data_changed)draw_page();
      else if(frequency_changed&&g_tab==Tab::airports&&g_detail_kind==DetailKind::none){
        const size_t first=g_airport_page*static_cast<size_t>(g_layout.airport_row_count);
        const size_t rows=std::min(static_cast<size_t>(g_layout.airport_row_count),g_snapshot.catalog_count-first);
        char value[128];for(size_t i=0;i<rows;++i){const auto&e=g_snapshot.catalog[first+i];
          std::snprintf(value,sizeof(value),"%9.3f MHz   %-9s   %5.1f nm   %-9s   %-34.34s",mhz(e.frequency_hz),service_name(e.service),double(e.distance_nm),source_class_name(e.source_class),e.label);
          const auto&r=g_layout.airport_rows[i];button(r,value,e.frequency_hz==g_snapshot.frequency_hz,true,2);}
      }
      break;
    case Tab::scope:
      if (frequency_changed) {
        g_scope_ceiling_valid = false;
        draw_scope_title();
        draw_scope_axis();
      }
      if (frequency_changed || (live_changed && now - g_last_meter_ms >= 250u) ||
          settings_changed) {
        draw_scope_status();
        g_last_meter_ms = now;
      }
      if (gain_changed || frequency_changed || settings_changed || state_changed ||
          filter_changed || squelch_state_changed)
        draw_scope_controls();
      break;
  }
  M5.Display.endWrite();
}

Action dashboard_handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if(g_detail_kind!=DetailKind::none){
    if(hit(x,y,g_layout.detail_close)){g_detail_kind=DetailKind::none;dashboard_draw();}
    return{};
  }
  if(audio_header::home_hit(x,y)){action_feedback(x,y);return{ActionKind::exit_home};}
  if(audio_header::settings_hit(x,y)){action_feedback(x,y);return{ActionKind::open_settings};}
  for(int i=0;i<kTabCount;++i)if(hit(x,y,g_layout.tab[i]))return tab_touch(x,y);
  Action action{};
  switch (g_tab) {
    case Tab::listen: action=listen_touch(x,y);break;
    case Tab::scan: action=scan_touch(x,y);break;
    case Tab::airports: action=airports_touch(x,y);break;
    case Tab::activity: action=activity_touch(x,y);break;
    case Tab::setup: action=setup_touch(x,y);break;
    case Tab::scope: action=scope_touch(x,y);break;
  }
  if(action.kind!=ActionKind::none)action_feedback(x,y);
  return action;
}

void dashboard_select_tab(Tab tab) {
  if (!g_active) return;
  if(tab!=g_tab)g_detail_kind=DetailKind::none;
  set_tab(tab);
  dashboard_draw();
}

Action dashboard_control(Control control,int32_t value,bool* applied){
  if(applied)*applied=false;
  if(!g_active)return{};
  if(control==Control::select_tab){if(value<0||value>=kTabCount)return{};dashboard_select_tab(static_cast<Tab>(value));if(applied)*applied=true;return{};}
  ActionKind kind=ActionKind::none;
  switch(control){
    case Control::tune_down:kind=ActionKind::tune_down;break;case Control::tune_up:kind=ActionKind::tune_up;break;
    case Control::tune_guard:kind=ActionKind::tune_guard;break;case Control::scan_toggle:kind=ActionKind::scan_toggle;break;
    case Control::hold_toggle:kind=ActionKind::hold_toggle;break;case Control::skip:kind=ActionKind::skip;break;
    case Control::tune_catalog:kind=ActionKind::tune_catalog;break;case Control::source_cycle:kind=ActionKind::source_cycle;break;
    case Control::spacing_cycle:kind=ActionKind::spacing_cycle;break;case Control::radius_cycle:kind=ActionKind::radius_cycle;break;
    case Control::gain_down:kind=ActionKind::gain_down;break;case Control::gain_up:kind=ActionKind::gain_up;break;
    case Control::tuner_agc_toggle:kind=ActionKind::tuner_agc_toggle;break;case Control::rtl_agc_toggle:kind=ActionKind::rtl_agc_toggle;break;
    case Control::squelch_down:kind=ActionKind::squelch_down;break;case Control::squelch_up:kind=ActionKind::squelch_up;break;
    case Control::settle_cycle:kind=ActionKind::settle_cycle;break;case Control::hang_down:kind=ActionKind::hang_down;break;
    case Control::hang_up:kind=ActionKind::hang_up;break;case Control::priority_toggle:kind=ActionKind::priority_toggle;break;
    case Control::reload_catalog:kind=ActionKind::reload_catalog;break;case Control::clear_activity:kind=ActionKind::clear_activity;break;
    case Control::open_settings:kind=ActionKind::open_settings;break;case Control::open_location_settings:kind=ActionKind::open_location_settings;break;
    case Control::exit_home:kind=ActionKind::exit_home;break;case Control::tune_to:kind=ActionKind::tune_to;break;
    case Control::span_down:kind=ActionKind::span_down;break;case Control::span_up:kind=ActionKind::span_up;break;
    case Control::filter_down:kind=ActionKind::filter_down;break;case Control::filter_up:kind=ActionKind::filter_up;break;
    case Control::filter_set:kind=ActionKind::filter_set;break;case Control::select_tab:break;
  }
  using receiver_controls::Availability;
  if(kind==ActionKind::none)return{};
  if(kind==ActionKind::tune_to&&(value<static_cast<int32_t>(kMinFrequencyHz)||value>static_cast<int32_t>(kMaxFrequencyHz)))return{};
  if(kind==ActionKind::filter_set&&(value<3000||value>30000||value%1000!=0))return{};
  if((kind==ActionKind::gain_down||kind==ActionKind::gain_up)&&receiver_controls::item(receiver_controls::Control::rf_gain,g_snapshot.controls).availability!=Availability::enabled)return{};
  if(kind==ActionKind::tuner_agc_toggle&&receiver_controls::item(receiver_controls::Control::tuner_agc,g_snapshot.controls).availability!=Availability::enabled)return{};
  if(kind==ActionKind::rtl_agc_toggle&&receiver_controls::item(receiver_controls::Control::rtl_agc,g_snapshot.controls).availability!=Availability::enabled)return{};
  if(kind==ActionKind::radius_cycle&&!g_snapshot.location_configured)return{};
  if(kind==ActionKind::clear_activity&&!g_snapshot.activity_count)return{};
  if((kind==ActionKind::hold_toggle||kind==ActionKind::skip)&&g_snapshot.scan_state==ScanState::off)return{};
  if(kind==ActionKind::tune_catalog){
    if(value<0){const int64_t index=-int64_t(value)-1;if(g_tab!=Tab::activity||index>=g_snapshot.activity_count)return{};}
    else if(g_tab==Tab::scan){if(static_cast<uint32_t>(value)>=g_snapshot.bank_count)return{};}
    else if(g_tab==Tab::airports){if(static_cast<uint32_t>(value)>=g_snapshot.catalog_count)return{};}
    else return{};
  }
  if(applied)*applied=true;
  return{kind,(kind==ActionKind::tune_to||kind==ActionKind::filter_set)?value:0};
}

bool dashboard_active() { return g_active; }
bool dashboard_spectrum_active() { return g_active && g_tab == Tab::scope; }

namespace {
// Half the filter width in pixels on the scope, for the current span.
int scope_half_filter_px() {
  if (g_scope_span_hz == 0) return 0;
  return std::clamp(static_cast<int>(static_cast<int64_t>(g_snapshot.filter_hz) * (kScopeW-2) /
                                     (2 * static_cast<int64_t>(g_scope_span_hz))),
                    2, (kScopeW-2) / 2 - 2);
}
}  // namespace

bool dashboard_scope_edge_hit(int32_t x, int32_t y) {
  if (!dashboard_spectrum_active() || g_scope_span_hz == 0) return false;
  if (y < g_layout.scope_plot.y || y >= g_layout.scope_waterfall.y+g_layout.scope_waterfall.h) return false;
  const int centre = kScopeX + kScopeW / 2;
  const int half = scope_half_filter_px();
  constexpr int kGrab = 26;   // generous: a fingertip is much wider than a one-pixel line
  return std::abs(x - (centre - half)) <= kGrab || std::abs(x - (centre + half)) <= kGrab;
}

uint32_t dashboard_scope_filter_from_x(int32_t x) {
  if (g_scope_span_hz == 0) return 0;
  const int centre = kScopeX + kScopeW / 2;
  const int64_t width = 2ll * std::abs(x - centre) * static_cast<int64_t>(g_scope_span_hz) / std::max(1,kScopeW-2);
  const int64_t rounded = (width + 500) / 1000 * 1000;
  return static_cast<uint32_t>(std::clamp<int64_t>(rounded, 3000, 30000));
}
void dashboard_set_scope_span_hook(void (*hook)(uint32_t hz)) { g_scope_span_hook = hook; }

void dashboard_draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                             uint32_t span_hz) {
  if (!dashboard_spectrum_active() || levels == nullptr || visible_bins < 2 || span_hz == 0) return;
  using waterfall_style::Screen;
  const int w=std::min(1230,kScopeW-2);
  const int x0=kScopeX+1;
  const uint32_t frame_started_ms = millis();
  if (span_hz != g_scope_span_hz) {
    g_scope_span_hz = span_hz;
    draw_scope_axis();
    draw_scope_status();
    draw_scope_controls();
  }
  float sum = 0.0f, strongest = -200.0f;
  for (int i = 0; i < w; ++i) {
    const float level = spectrum::peak_for_pixel(levels, first_bin, visible_bins,
                                                 static_cast<size_t>(i), static_cast<size_t>(w));
    // Light frame-to-frame smoothing so the trace is steady like the FM and Home scopes.
    g_scope_levels[i] = g_scope_levels_valid
                            ? kScopeSmoothing * level + (1.0f - kScopeSmoothing) * g_scope_levels[i]
                            : level;
    sum += g_scope_levels[i];
    strongest = std::max(strongest, g_scope_levels[i]);
  }
  g_scope_levels_valid = true;
  // Floor tracks the average (mostly noise); the ceiling follows the strongest bin quickly and falls
  // slowly so a transmission does not clip and the scale does not pump.
  const float floor_db = sum / static_cast<float>(w) - 3.0f;
  g_scope_ceiling = (!g_scope_ceiling_valid || strongest > g_scope_ceiling)
                        ? strongest
                        : g_scope_ceiling + (strongest - g_scope_ceiling) * 0.05f;
  g_scope_ceiling_valid = true;
  const float range_db = std::max(kScopeMinRangeDb, g_scope_ceiling + kScopeHeadroomDb - floor_db);

  // Draw the spectrum off-screen and push it in one go. Erasing and redrawing the screen area every
  // frame made the trace and the filter lines flicker.
  const int canvas_h=std::max(1,kScopeSpecH-2);
  if(g_scope_canvas&&(g_scope_canvas_w!=w||g_scope_canvas_h!=canvas_h)){
    g_scope_canvas->deleteSprite();g_scope_canvas_w=g_scope_canvas_h=0;
  }
  if(!g_scope_canvas){g_scope_canvas=new M5Canvas(&M5.Display);g_scope_canvas->setPsram(true);g_scope_canvas->setColorDepth(16);}
  if(g_scope_canvas&&g_scope_canvas_w==0&&g_scope_canvas->createSprite(w,canvas_h)){g_scope_canvas_w=w;g_scope_canvas_h=canvas_h;}
  M5Canvas* canvas=(g_scope_canvas_w==w&&g_scope_canvas_h==canvas_h)?g_scope_canvas:nullptr;
  if (canvas != nullptr) {
    canvas->fillSprite(TFT_BLACK);
    for (int k = 1; k < 5; ++k) canvas->drawFastHLine(0, k * canvas_h / 5 - 1, w, kGrid);
    for (int k = 1; k < 8; ++k) canvas->drawFastVLine(k * kScopeW / 8 - 1, 0, canvas_h, kGrid);
    const int base = canvas_h - 3;
    const int height = canvas_h - 7;
    int px = 0, py = base;
    for (int k = 0; k < w; ++k) {
      const float normalized = std::clamp((g_scope_levels[k] - floor_db) / range_db, 0.0f, 1.0f);
      const int y = base - static_cast<int>(normalized * height);
      if (k) canvas->drawLine(px, py, k, y, kGreen);
      px = k;
      py = y;
    }
    const int centre = w / 2;
    const int half_filter = scope_half_filter_px();
    canvas->drawFastVLine(centre, 0, canvas_h, kCyan);
    canvas->drawFastVLine(centre - half_filter, 0, canvas_h, kYellow);
    canvas->drawFastVLine(centre + half_filter, 0, canvas_h, kYellow);
    canvas->drawFastVLine(centre - half_filter - 1, 0, canvas_h, kYellow);   // 2 px wide: easier to see
    canvas->drawFastVLine(centre + half_filter + 1, 0, canvas_h, kYellow);
    canvas->pushSprite(x0,g_layout.scope_plot.y+1);
  }

  M5.Display.startWrite();
  const int rows = waterfall_style::rows_due(Screen::airband, millis());
  M5.Display.setScrollRect(x0,g_layout.scope_waterfall.y+1,w,g_layout.scope_waterfall.h-2,TFT_BLACK);
  if (rows) M5.Display.scroll(0, -rows);
  for (int i = 0; i < w; ++i)
    g_scope_row[i] = waterfall_style::color565(
        Screen::airband,
        std::clamp((g_scope_levels[i] - floor_db) / kScopeWaterfallRangeDb, 0.0f, 1.0f));
  for (int row = 0; row < rows; ++row)
    // Native RGB565 row: push it as rgb565_t (a uint16_t* image is read as big-endian and shows wrong hues).
    M5.Display.pushImage(x0,g_layout.scope_waterfall.y+g_layout.scope_waterfall.h-1-rows+row,w,1,
                         reinterpret_cast<const lgfx::rgb565_t*>(g_scope_row));
  M5.Display.endWrite();
  g_scope_draw_ms = millis() - frame_started_ms;
  ++g_scope_frames;
  if (frame_started_ms - g_scope_window_ms >= 1000u) {
    g_scope_fps = g_scope_frames;
    g_scope_frames = 0;
    g_scope_window_ms = frame_started_ms;
  }
}
uint32_t dashboard_scope_fps() { return g_scope_fps; }
uint32_t dashboard_scope_draw_ms() { return g_scope_draw_ms; }
Tab dashboard_tab() { return g_tab; }

bool dashboard_self_check() {
  const bool was_active = g_active;
  const Tab saved_tab = g_tab;
  EXT_RAM_BSS_ATTR static Snapshot saved;
  saved = g_snapshot;
  g_active = true;
  reset_snapshot(g_snapshot);
  g_snapshot.scan_state = ScanState::scanning;
  g_snapshot.catalog_count = 1;
  g_snapshot.catalog[0].frequency_hz = 124900000u;
  g_snapshot.activity_count = 1;
  g_snapshot.activity[0].frequency_hz = 125800000u;

  bool ok = listen_touch(cx(control(0, 0)), cy(control(0, 0))).kind ==
                ActionKind::scan_toggle &&
            listen_touch(cx(control(1, 1)), cy(control(1, 1))).kind ==
                ActionKind::tune_guard &&
            airports_touch(cx(list_row(0)), cy(list_row(0))).value == 0 &&
            activity_touch(cx(list_row(0)), cy(list_row(0))).value == -1 &&
            setup_touch(cx(setup_rect(1,0)),
                        cy(setup_rect(1,0))).kind ==
                ActionKind::spacing_cycle &&
            listen_touch(cx(kGainUp), cy(kGainUp)).kind == ActionKind::none &&
            kGainCard.y + kGainCard.h <= kTabsY &&
            kStatusCard.y + kStatusCard.h <= kGainCard.y &&
            kTabsY + 90 <= 720 &&
            kStatusCard.y + kStatusCard.h <= kTabsY &&
            kScopeWfY + kScopeWfH <= kScopeRowAY - 4 && kScopeRowBY + kScopeRowH <= kTabsY && kScopeAxisY + kScopeAxisH <= kScopeWfY &&
            g_layout.tab[5].x+g_layout.tab[5].w<=g_layout.viewport.x+g_layout.viewport.w;

  g_snapshot = saved;
  g_tab = saved_tab;
  g_active = was_active;
  return ok;
}

}  // namespace orcsdr::airband

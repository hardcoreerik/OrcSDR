#include "focus_nav.hpp"
#include "shortwave_dashboard.hpp"
#include "shortwave_dashboard_state.hpp"
#include "shortwave_dashboard_layout.hpp"

#include "dashboard_audio_control.hpp"
#include "freq_keypad.hpp"
#include "shortwave_model.hpp"
#include "scope_canvas.hpp"
#include "waterfall_style.hpp"
#include "spectrum_resample.hpp"
#include "text_editor.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::shortwave {
namespace {

constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
struct Rect {
  int x;
  int y;
  int w;
  int h;
};

int kSpectrumX = 24, kSpectrumY = 276, kSpectrumClosedW = 1232;
int kSpectrumOpenW = 804, kSpectrumH = 204, kSpectrumLabelH = 20;
int kSpectrumPlotH = kSpectrumH-kSpectrumLabelH;
int kWaterfallX=24,kWaterfallY = 486, kWaterfallH = 102, kZoomY = 592, kTabsY = 630, kTabW = 256;
Rect kFrequencyCard{24,104,1232,80}, kFrequencyMinus{42,112,64,64};
Rect kFrequencyEntry{120,108,1040,72}, kFrequencyPlus{1174,112,64,64};
Rect kStep{24,190,130,52}, kFilterDown{162,190,52,52}, kFilterValue{222,190,150,52};
Rect kFilterUp{380,190,52,52}, kMode{440,190,100,52}, kClean{548,190,86,52};
Rect kBoost{642,190,86,52}, kNoiseReduction{736,190,96,52}, kNotch{840,190,96,52};
Rect kSquelch{944,190,130,52}, kTuner{1082,190,174,52};
Rect kClarifierDown{130,146,56,30}, kClarifierUp{304,146,56,30};
Rect kDrawer{840,104,420,522}, kDrawerClose{858,552,378,52};
Rect kTunerAgc{858,176,184,68}, kRtlAgc{1052,176,184,68};
int kGainX=872,kGainY=442,kGainW=350;
layout::Layout g_layout=layout::make_layout({0,0,1280,720});
layout::Rect g_viewport{0,0,1280,720};

Rect local(layout::Rect r) { return {r.x,r.y,r.w,r.h}; }
void install_geometry(const layout::Layout& l) {
  g_layout=l; g_viewport=l.viewport;
  kSpectrumX=l.spectrum.x;kSpectrumY=l.spectrum.y;kSpectrumClosedW=l.spectrum.w;
  kSpectrumH=l.spectrum.h;kSpectrumLabelH=20;kSpectrumPlotH=std::max(48,kSpectrumH-kSpectrumLabelH);
  kWaterfallX=l.waterfall.x;kWaterfallY=l.waterfall.y;kWaterfallH=l.waterfall.h;kZoomY=l.zoom_minus.y;
  kTabsY=l.tabs.y;kTabW=l.tabs.w/5;
  kFrequencyCard=local(l.frequency);kFrequencyMinus=local(l.frequency_minus);
  kFrequencyEntry=local(l.frequency_value);kFrequencyPlus=local(l.frequency_plus);
  kStep=local(l.quick[0]);kFilterDown=local(l.quick[1]);kFilterValue=local(l.quick[2]);
  kFilterUp=local(l.quick[3]);kMode=local(l.quick[4]);kClean=local(l.quick[5]);
  kBoost=local(l.quick[6]);kNoiseReduction=local(l.quick[7]);kNotch=local(l.quick[8]);
  kSquelch=local(l.quick[9]);kTuner=local(l.quick[10]);
  kDrawer=local(l.drawer);kDrawerClose=local(l.drawer_close);kTunerAgc=local(l.tuner_agc);
  kRtlAgc=local(l.rtl_agc);kGainX=l.gain_slider.x;kGainY=l.gain_slider.y;kGainW=l.gain_slider.w;
  if(l.legacy){kSpectrumOpenW=804;kClarifierDown={130,146,56,30};kClarifierUp={304,146,56,30};return;}
  kSpectrumOpenW=std::max(96,l.drawer.x-l.spectrum.x);
  kClarifierDown={kFilterDown.x,kFilterDown.y,kFilterDown.w,kFilterDown.h};
  kClarifierUp={kFilterUp.x,kFilterUp.y,kFilterUp.w,kFilterUp.h};
}

Snapshot g_snapshot{};
bool g_active = false;
bool g_static_dirty = true;
Tab g_drawn_tab=Tab::live;
Modal g_drawn_modal=Modal::none;
bool g_drawn_drawer=false;
// dashboard_self_check() drives the real touch handlers at boot; keep it off
// the display so the boot sequence does not flash Shortwave tabs.
bool g_self_check_running = false;
DashboardState g_state{};
uint32_t g_saved_frequency = 7100000;
size_t g_hunt_band = 0;
size_t g_hunt_page = 0;
size_t g_on_air_page = 0;
bool g_hunt_start_pending=false;
uint32_t g_hunt_pending_since=0;
char g_entry[12]{};
char g_pending_memory_label[64]{};
char g_pending_memory_notes[160]{};
char g_pending_log_antenna[64]{};
char g_pending_log_notes[240]{};
size_t g_edit_memory = SIZE_MAX;
size_t g_edit_log = SIZE_MAX;
size_t g_memory_page = 0;
size_t g_log_page = 0;
size_t g_delete_memory = SIZE_MAX;
size_t g_delete_log = SIZE_MAX;
bool g_filter_dragging = false;
bool g_tuner_drawer_open = false;
EXT_RAM_BSS_ATTR uint16_t g_waterfall_row[1232]{};

int spectrum_width() {
  return g_tuner_drawer_open ? kSpectrumOpenW : kSpectrumClosedW;
}

bool hit(int32_t x, int32_t y, const Rect& rect) {
  return x >= rect.x && x < rect.x + rect.w &&
         y >= rect.y && y < rect.y + rect.h;
}

bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && a.x + a.w > b.x &&
         a.y < b.y + b.h && a.y + a.h > b.y;
}

int center_x(const Rect& rect) { return rect.x + rect.w / 2; }
int center_y(const Rect& rect) { return rect.y + rect.h / 2; }

int spectrum_x_for_bin(size_t bin, size_t visible_bins) {
  return kSpectrumX + static_cast<int>(bin * (spectrum_width() - 1) /
                                       (visible_bins > 1 ? visible_bins - 1 : 1));
}

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

uint32_t spectrum_tick_frequency(uint32_t center_hz, uint32_t span_hz,
                                 size_t tick, size_t tick_count) {
  if (tick_count < 2) return center_hz;
  const uint32_t half_span = span_hz / 2u;
  const uint32_t first = center_hz > half_span ? center_hz - half_span : 0u;
  return first + static_cast<uint32_t>(
      static_cast<uint64_t>(span_hz) * tick / (tick_count - 1u));
}

int filter_half_width() {
  const int width = spectrum_width();
  return std::clamp(static_cast<int>(
      static_cast<uint64_t>(g_snapshot.filter_bandwidth_hz) * width /
      (2u * (g_snapshot.span_hz ? g_snapshot.span_hz : 1u))), 3,
      width / 2 - 2);
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          int size = 2, textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void card(int x, int y, int w, int h) {
  M5.Display.fillRoundRect(x, y, w, h, 10, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 10, kCyan);
}

void button(int x, int y, int w, int h, const char* label, bool selected = false,
            bool enabled = true) {
  if (enabled) orcsdr::focus_nav::note(x, y, w, h);
  const uint16_t color = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(x, y, w, h, 8, selected ? 0x1264 : kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 8, color);
  text(label, x + w / 2, y + h / 2, enabled ? TFT_WHITE : kMuted, w<88?1:2);
}

void button(const Rect& rect, const char* label, bool selected = false,
            bool enabled = true) {
  button(rect.x, rect.y, rect.w, rect.h, label, selected, enabled);
}

const char* route_name(ReceiverRoute route) {
  switch (route) {
    case ReceiverRoute::direct_q: return "DIRECT Q SAMPLING";
    case ReceiverRoute::hf_upconverter: return "HF UPCONVERTER";
    case ReceiverRoute::tuner: return "NORMAL TUNER";
    default: return "ROUTE UNKNOWN";
  }
}

bool ssb_mode() {
  return std::strcmp(g_snapshot.mode, "USB") == 0 || std::strcmp(g_snapshot.mode, "LSB") == 0;
}

void draw_frequency() {
  const int width = g_layout.legacy?spectrum_width():g_layout.frequency.w;
  M5.Display.fillRect(kSpectrumX + 1, kFrequencyCard.y + 1,
                      width - 2, kFrequencyCard.h - 2, kPanel);
  button(kFrequencyMinus, "-");
  const Rect plus=g_layout.legacy?Rect{kSpectrumX+width-82,kFrequencyPlus.y,kFrequencyPlus.w,kFrequencyPlus.h}:local(g_layout.frequency_plus);
  button(plus, "+");
  char value[40];
  snprintf(value, sizeof(value), "%lu.%03lu.%03lu MHz",
           static_cast<unsigned long>(g_snapshot.frequency_hz / 1000000u),
           static_cast<unsigned long>((g_snapshot.frequency_hz / 1000u) % 1000u),
           static_cast<unsigned long>(g_snapshot.frequency_hz % 1000u));
  const bool legacy=g_layout.legacy;
  text(value, kSpectrumX + width / 2, legacy?132:kFrequencyCard.y+(kFrequencyCard.h>=70?26:20),
       TFT_WHITE, legacy?4:2);
  const BroadcastBand* sw_band = band_for(g_snapshot.frequency_hz);
  const ModeGuide guide = mode_guide_for(g_snapshot.frequency_hz);
  char context[64];
  snprintf(context, sizeof(context), "%s  |  %s %s",
           sw_band ? sw_band->label : region_label(region_for(g_snapshot.frequency_hz)),
           guide.likely_mode, guide.supported_now ? "READY" : "GUIDE");
  text(context, kSpectrumX + width / 2, legacy?166:kFrequencyCard.y+(kFrequencyCard.h>=70?58:44),
       guide.supported_now ? kGreen : kYellow, legacy?2:1);
  if (ssb_mode() && legacy) {
    button(kClarifierDown, "-");
    snprintf(value, sizeof(value), "CLAR %+d Hz", static_cast<int>(g_snapshot.clarifier_hz));
    text(value, kClarifierDown.x + (kClarifierUp.x + kClarifierUp.w - kClarifierDown.x) / 2, 161, kCyan, 2);
    button(kClarifierUp, "+");
  }
}

void draw_status() {
  const int width = spectrum_width();
  const Rect status_rect=g_layout.legacy?Rect{kSpectrumX,246,width,26}:local(g_layout.live_status);
  M5.Display.fillRect(status_rect.x,status_rect.y,status_rect.w,status_rect.h,TFT_BLACK);
  char status[96];
  snprintf(status, sizeof(status), "%s  |  %+.1f dBFS  |  %lu kHz span%s",
           g_snapshot.running ? "LIVE IQ" : "WAITING",
           static_cast<double>(g_snapshot.relative_dbfs),
           static_cast<unsigned long>(g_snapshot.span_hz / 1000u),
           g_snapshot.clipping_percent > 0.1f ? "  |  CLIP" : "");
  text(status, kSpectrumX + width / 2, g_layout.legacy?259:status_rect.y+status_rect.h/2,
       g_snapshot.clipping_percent > 0.1f ? TFT_RED
                                         : g_snapshot.running ? kGreen : TFT_ORANGE,
       2);
}

void draw_quick_controls() {
  char value[40];
  snprintf(value, sizeof(value), "STEP %lu Hz",
           static_cast<unsigned long>(g_snapshot.step_hz));
  button(kStep, value);
  if(ssb_mode()&&!g_layout.legacy){
    char clarifier[32];snprintf(clarifier,sizeof(clarifier),"CLAR %+d",int(g_snapshot.clarifier_hz));
    button(kFilterDown,"-");button(kFilterValue,clarifier,true);button(kFilterUp,"+");
  } else {
    button(kFilterDown, "<", false, !ssb_mode());
  snprintf(value, sizeof(value), "FILTER %.1f kHz",
           static_cast<double>(g_snapshot.filter_bandwidth_hz) / 1000.0);
  button(kFilterValue, value, false, !ssb_mode());
  button(kFilterUp, ">", false, !ssb_mode());
  }
  snprintf(value, sizeof(value), g_snapshot.mode_auto ? "AUTO %s" : "%s", g_snapshot.mode);
  button(kMode, value, ssb_mode());
  const auto boost = receiver_controls::item(receiver_controls::Control::audio_boost,
                                              g_snapshot.controls);
  const bool clean = g_snapshot.filter_bandwidth_hz == 6000 &&
      g_snapshot.dsp.noise_reduction == audio_dsp::NoiseReduction::low &&
      g_snapshot.dsp.auto_notch &&
      g_snapshot.dsp.squelch == audio_dsp::SquelchMode::off;
  const char* nr = g_snapshot.dsp.noise_reduction == audio_dsp::NoiseReduction::off
                       ? "NR OFF"
                       : g_snapshot.dsp.noise_reduction == audio_dsp::NoiseReduction::low
                             ? "NR LOW"
                             : "NR HIGH";
  char sql[32];
  if (g_snapshot.dsp.squelch == audio_dsp::SquelchMode::off)
    snprintf(sql, sizeof(sql), "SQL OFF");
  else if (g_snapshot.dsp.squelch == audio_dsp::SquelchMode::automatic)
    snprintf(sql, sizeof(sql), "SQL AUTO");
  else
    snprintf(sql, sizeof(sql), "SQL %d dB", g_snapshot.dsp.squelch_dbfs);
  button(kClean, "CLEAN", clean, g_snapshot.audio_dsp_available);
  button(kBoost, "BOOST", boost.active, g_snapshot.audio_dsp_available);
  button(kNoiseReduction, nr,
         g_snapshot.dsp.noise_reduction != audio_dsp::NoiseReduction::off,
         g_snapshot.audio_dsp_available);
  button(kNotch, "NOTCH", g_snapshot.dsp.auto_notch,
         g_snapshot.audio_dsp_available);
  button(kSquelch, sql,
         g_snapshot.dsp.squelch != audio_dsp::SquelchMode::off &&
             g_snapshot.dsp_metrics.squelch_open,
         g_snapshot.audio_dsp_available);
  button(kTuner, "TUNER", g_tuner_drawer_open);
}

void draw_zoom_controls() {
  char span[32];
  snprintf(span, sizeof(span), "SPAN %lu kHz",
           static_cast<unsigned long>(g_snapshot.span_hz / 1000u));
  const int width = spectrum_width();
  if(g_layout.legacy){button(kSpectrumX,kZoomY,160,34,"ZOOM IN");text(span,kSpectrumX+width/2,kZoomY+17,kCyan,2);button(kSpectrumX+width-160,kZoomY,160,34,"ZOOM OUT");}
  else {button(local(g_layout.zoom_minus),"ZOOM IN");text(span,g_layout.span_value.x+g_layout.span_value.w/2,g_layout.span_value.y+g_layout.span_value.h/2,kCyan,1);button(local(g_layout.zoom_plus),"ZOOM OUT");}
}

void draw_spectrum_scale() {
  constexpr size_t kTicks = 5;
  const int width = spectrum_width();
  M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + kSpectrumPlotH,
                      width - 2, kSpectrumLabelH - 1, TFT_BLACK);
  char value[20];
  for (size_t tick = 0; tick < kTicks; ++tick) {
    const int x = kSpectrumX + static_cast<int>(
        tick * (width - 1) / (kTicks - 1));
    const uint32_t frequency = spectrum_tick_frequency(
        g_snapshot.frequency_hz, g_snapshot.span_hz, tick, kTicks);
    snprintf(value, sizeof(value), "%.3f", static_cast<double>(frequency) / 1.0e6);
    const textdatum_t datum = tick == 0 ? top_left
                              : tick + 1 == kTicks ? top_right
                                                   : middle_center;
    text(value, x, kSpectrumY + kSpectrumPlotH + 10,
         tick == kTicks / 2 ? kCyan : TFT_LIGHTGREY, 1, datum);
  }
}

void draw_tuner_drawer() {
  if (!g_tuner_drawer_open) return;
  const auto tuner = receiver_controls::item(receiver_controls::Control::tuner_agc,
                                              g_snapshot.controls);
  const auto rtl = receiver_controls::item(receiver_controls::Control::rtl_agc,
                                            g_snapshot.controls);
  const auto gain = receiver_controls::item(receiver_controls::Control::rf_gain,
                                             g_snapshot.controls);
  card(kDrawer.x, kDrawer.y, kDrawer.w, kDrawer.h);
  if(!g_layout.legacy){
    const int cx=kDrawer.x+kDrawer.w/2;
    text(route_name(g_snapshot.controls.route),cx,kDrawer.y+24,kGreen,kDrawer.w<240?1:2);
    text(g_snapshot.device[0]?g_snapshot.device:"NO RTL-SDR",cx,kDrawer.y+48,g_snapshot.driver_ready?TFT_WHITE:TFT_ORANGE,1);
    button(kTunerAgc,"TUNER AGC",tuner.active,tuner.availability==receiver_controls::Availability::enabled);
    button(kRtlAgc,"RTL AGC",rtl.active,rtl.availability==receiver_controls::Availability::enabled);
    const int body_y=kDrawer.y+148,body_h=std::max(44,kDrawer.y+kDrawer.h-body_y-52);
    M5.Display.fillRect(kDrawer.x+8,body_y,kDrawer.w-16,body_h,kPanel);
    if(tuner.availability!=receiver_controls::Availability::enabled)text(tuner.explanation,cx,body_y+18,kMuted,1);
    if(rtl.availability!=receiver_controls::Availability::enabled)text(rtl.explanation,cx,body_y+38,kMuted,1);
    if(gain.availability!=receiver_controls::Availability::enabled){
      text("NO RF GAIN STAGE",cx,body_y+36,kMuted,1);
      text("RTL AGC is separate",cx,body_y+58,TFT_LIGHTGREY,1);
      button(kDrawerClose,"CLOSE");return;
    }
    text("RF GAIN",kDrawer.x+12,body_y+6,kCyan,1,top_left);
    char current[32];snprintf(current,sizeof(current),g_snapshot.controls.tuner_agc?"AUTO %.1f dB":"MANUAL %.1f dB",double(g_snapshot.controls.gain_tenth_db)/10.0);
    text(current,cx,body_y+30,g_snapshot.controls.tuner_agc?kGreen:TFT_WHITE,1);
    focus_nav::note(kGainX-16,kGainY-20,kGainW+32,62);
    M5.Display.drawRoundRect(kGainX,kGainY,kGainW,22,10,kCyan);
    int pos=0;if(g_snapshot.gain_step_count>1){size_t nearest=0;for(size_t i=1;i<g_snapshot.gain_step_count;++i)if(std::abs(g_snapshot.gain_steps_tenth_db[i]-g_snapshot.controls.gain_tenth_db)<std::abs(g_snapshot.gain_steps_tenth_db[nearest]-g_snapshot.controls.gain_tenth_db))nearest=i;pos=int(nearest*kGainW/(g_snapshot.gain_step_count-1));}
    M5.Display.fillCircle(kGainX+pos,kGainY+11,12,g_snapshot.controls.tuner_agc?kMuted:kGreen);
    text("AGC AUTO  |  DRAG FOR GAIN",cx,kDrawer.y+kDrawer.h-64,TFT_LIGHTGREY,1);
    button(kDrawerClose,"CLOSE");return;
  }
  text(route_name(g_snapshot.controls.route), 1050, 126, kGreen, 2);
  text(g_snapshot.device[0] ? g_snapshot.device : "NO RTL-SDR", 1050, 151,
       g_snapshot.driver_ready ? TFT_WHITE : TFT_ORANGE, 1);
  button(kTunerAgc, "TUNER AGC", tuner.active,
         tuner.availability == receiver_controls::Availability::enabled);
  button(kRtlAgc, "RTL AGC", rtl.active,
         rtl.availability == receiver_controls::Availability::enabled);
  M5.Display.fillRect(858, 258, 378, 282, kPanel);
  if (tuner.availability != receiver_controls::Availability::enabled)
    text(tuner.explanation, 1047, 274, kMuted, 1);
  if (rtl.availability != receiver_controls::Availability::enabled)
    text(rtl.explanation, 1047, 296, kMuted, 1);
  text("RF GAIN", 858, 318,
       gain.availability == receiver_controls::Availability::enabled ? kCyan : kMuted,
       2, top_left);
  if (gain.availability != receiver_controls::Availability::enabled) {
    text(gain.explanation, 1047, 374, kMuted, 2);
    text("This receiver route has no tuner gain stage", 1047, 410,
         TFT_LIGHTGREY, 1);
    text("RTL AGC remains available when supported", 1047, 432,
         TFT_LIGHTGREY, 1);
    button(kDrawerClose, "CLOSE");
    return;
  }
  char gain_value[32];
  snprintf(gain_value, sizeof(gain_value), g_snapshot.controls.tuner_agc
                                                   ? "AUTO %.1f dB"
                                                   : "MANUAL %.1f dB",
           static_cast<double>(g_snapshot.controls.gain_tenth_db) / 10.0);
  text(gain_value, 1047, 370, g_snapshot.controls.tuner_agc ? kGreen : TFT_WHITE, 2);
  focus_nav::note(kGainX-16,kGainY-20,kGainW+32,62);
  M5.Display.drawRoundRect(kGainX, kGainY, kGainW, 22, 10, kCyan);
  int position = 0;
  if (g_snapshot.gain_step_count > 1) {
    size_t nearest = 0;
    for (size_t i = 1; i < g_snapshot.gain_step_count; ++i)
      if (std::abs(g_snapshot.gain_steps_tenth_db[i] - g_snapshot.controls.gain_tenth_db) <
          std::abs(g_snapshot.gain_steps_tenth_db[nearest] - g_snapshot.controls.gain_tenth_db))
        nearest = i;
    position = static_cast<int>(nearest * kGainW / (g_snapshot.gain_step_count - 1));
  }
  M5.Display.fillCircle(kGainX + position, kGainY + 11, 12,
                        g_snapshot.controls.tuner_agc ? kMuted : kGreen);
  text("Tap TUNER AGC for auto; drag for manual", 1047, 486, TFT_LIGHTGREY, 1);
  button(kDrawerClose, "CLOSE");
}

void draw_live_static() {
  const int width = spectrum_width();
  card(kFrequencyCard.x, kFrequencyCard.y, width, kFrequencyCard.h);
  M5.Display.drawRect(kSpectrumX, kSpectrumY, width, kSpectrumH, kGrid);
  for (int i = 1; i < 8; ++i)
    M5.Display.drawFastVLine(kSpectrumX + i * width / 8, kSpectrumY,
                            kSpectrumPlotH, kGrid);
  for (int i = 1; i < 4; ++i)
    M5.Display.drawFastHLine(kSpectrumX, kSpectrumY + i * kSpectrumPlotH / 4,
                            width, kGrid);
  draw_spectrum_scale();
  M5.Display.drawRect(kWaterfallX, kWaterfallY, g_layout.waterfall.w, kWaterfallH, kCyan);
  M5.Display.setScrollRect(kWaterfallX + 1, kWaterfallY + 1, g_layout.waterfall.w - 2,
                           kWaterfallH - 2, TFT_BLACK);
  draw_zoom_controls();
}

void draw_static() {
  M5.Display.clearScrollRect();
  focus_nav::clear();
  M5.Display.fillScreen(TFT_BLACK);
  audio_header::draw_brand(g_layout.legacy?"SHORTWAVE EXPLORER":"SHORTWAVE");
  if(g_layout.legacy){M5.Display.drawFastVLine(350,18,58,kCyan);text("SHORTWAVE",390,42,TFT_WHITE,4,middle_left);}
  audio_header::draw_battery(g_snapshot.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(g_snapshot.sound_enabled);
  audio_header::draw_visualizer_button(g_snapshot.running);
  audio_header::draw_settings_button();
  const auto& header=audio_header::layout();
  M5.Display.drawFastHLine(g_layout.legacy?20:g_viewport.x+4,
      g_layout.legacy?92:header.content_top-6,
      g_layout.legacy?1240:g_viewport.w-8,kGreen);

  if (g_state.tab() == Tab::live && g_state.modal() == Modal::none)
    draw_live_static();

  constexpr const char* tabs[] = {"LIVE", "ON AIR", "HUNT", "MEMORY", "LOGBOOK"};
  for (int i = 0; i < 5; ++i) {
    button(local(g_layout.tab[i]), tabs[i], static_cast<int>(g_state.tab()) == i);
  }
}

void draw_page_title(const char* title, const char* subtitle,bool clear_content=true) {
  M5.Display.clearScrollRect();
  if(clear_content)M5.Display.fillRect(g_layout.content.x,g_layout.content.y,g_layout.content.w,g_layout.content.h,TFT_BLACK);
  text(title, g_layout.page_title.x, g_layout.page_title.y+g_layout.page_title.h/2,
       TFT_WHITE, g_layout.portrait ? 2 : 4, middle_left);
  text(subtitle, g_layout.page_subtitle.x, g_layout.page_subtitle.y+g_layout.page_subtitle.h/2,
       kMuted, g_layout.portrait ? 1 : 2, middle_left);
}

void draw_storage_status() {
  const bool ready = g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready;
  M5.Display.fillRect(g_layout.storage_status.x,g_layout.storage_status.y,g_layout.storage_status.w,g_layout.storage_status.h,TFT_BLACK);
  text(ready ? "SD LIBRARY READY" : "SD LIBRARY UNAVAILABLE",
       g_layout.storage_status.x+g_layout.storage_status.w, g_layout.storage_status.y+g_layout.storage_status.h/2,
       ready ? kGreen : TFT_ORANGE, g_layout.portrait ? 1 : 2, middle_right);
}

void draw_empty_state(const char* title,const char* subtitle,Rect legacy_card,
                      int legacy_title_y,int legacy_subtitle_y,uint16_t title_color=TFT_WHITE,
                      uint16_t subtitle_color=kMuted) {
  if(g_layout.legacy){card(legacy_card.x,legacy_card.y,legacy_card.w,legacy_card.h);
    text(title,legacy_card.x+legacy_card.w/2,legacy_title_y,title_color,3);
    text(subtitle,legacy_card.x+legacy_card.w/2,legacy_subtitle_y,subtitle_color,2);return;}
  const Rect content=local(g_layout.content);const int pad=8;
  Rect box{content.x+pad,content.y+96,content.w-2*pad,std::max(100,content.h-212)};
  card(box.x,box.y,box.w,box.h);
  text(title,box.x+box.w/2,box.y+box.h/2-18,title_color,g_layout.portrait?2:2);
  text(subtitle,box.x+box.w/2,box.y+box.h/2+18,subtitle_color,1);
}

void draw_on_air(bool clear_content=true) {
  draw_page_title("ON AIR", "Local schedules are suggestions, never decoded identity",clear_content);
  if(!clear_content){const int y=g_layout.content.y+96;M5.Display.fillRect(g_layout.content.x,y,g_layout.content.w,g_layout.content.y+g_layout.content.h-y,TFT_BLACK);}
  draw_storage_status();
  if (!g_snapshot.utc_valid) {
    draw_empty_state("SET UTC TO USE ON AIR","Schedules stay hidden until the device clock is valid.",{36,230,1208,250},315,370,TFT_ORANGE,TFT_WHITE);
    return;
  }
  const size_t max_rows=static_cast<size_t>(g_layout.onair_row_count);
  size_t matches=0;
  for(size_t i=0;i<station_count();++i){const StationCard* s=station_at(i);if(s&&schedule_matches(*s,g_snapshot.frequency_hz,g_snapshot.utc_minute,g_snapshot.utc_weekday))++matches;}
  const size_t pages=std::max<size_t>(1,(matches+max_rows-1)/max_rows);g_on_air_page=std::min(g_on_air_page,pages-1);
  if(matches>max_rows){button(local(g_layout.hunt_previous),"<",false,g_on_air_page>0);char label[48];snprintf(label,sizeof(label),"PAGE %u / %u  |  %u MATCHES",unsigned(g_on_air_page+1),unsigned(pages),unsigned(matches));button(local(g_layout.hunt_page),label,true);button(local(g_layout.hunt_next),">",false,g_on_air_page+1<pages);}
  size_t row = 0,match_index=0;const size_t first=g_on_air_page*max_rows;
  for (size_t i = 0; i < station_count() && row < max_rows; ++i) {
    const StationCard* station = station_at(i);
    if (!station || !schedule_matches(*station, g_snapshot.frequency_hz,
                                      g_snapshot.utc_minute,
                                      g_snapshot.utc_weekday))
      continue;
    if(match_index++<first)continue;
    char label[160];
    snprintf(label, sizeof(label), "%.3f MHz  %s  |  %s",
             static_cast<double>(station->frequency_hz) / 1000000.0,
             station->station, station->program);
    if(g_layout.legacy)button(36,220+static_cast<int>(row)*58,1208,50,label,row==0);
    else button(local(g_layout.rows[row]),label,row==0);
    ++row;
  }
  if (!row) {
    draw_empty_state("NO MATCHING LOCAL SCHEDULE","Tune near a listed station frequency or use HUNT to explore.",{36,230,1208,250},315,370,TFT_ORANGE,TFT_WHITE);
  }
}

void draw_hunt() {
  draw_page_title("HUNT",g_snapshot.hunt_available?"Scan one broadcast band and keep the strongest candidates":"HUNT SERVICE UNAVAILABLE ON THIS RECEIVER");
  const BroadcastBand* selected = band(g_hunt_band);
  char selection[64];
  snprintf(selection, sizeof(selection), "BAND  %s  %.3f-%.3f MHz",
           selected ? selected->label : "--",
           selected ? static_cast<double>(selected->min_hz) / 1000000.0 : 0.0,
           selected ? static_cast<double>(selected->max_hz) / 1000000.0 : 0.0);
  if(g_layout.legacy){button(36,190,80,58,"<",false,g_snapshot.hunt_available);button(126,190,540,58,selection,true,g_snapshot.hunt_available);
    button(676,190,80,58,">",false,g_snapshot.hunt_available);}
  else {button(local(g_layout.page_controls[0]),"<",false,g_snapshot.hunt_available);button(local(g_layout.page_controls[1]),selection,true,g_snapshot.hunt_available);
    button(local(g_layout.page_controls[2]),">",false,g_snapshot.hunt_available);}
  char progress[48];
  snprintf(progress, sizeof(progress), "%u / %u", g_snapshot.hunt_step,
           g_snapshot.hunt_total);
  if(g_layout.legacy)text(progress,1170,165,g_snapshot.hunt_active?kGreen:kMuted,2);
  else {const Rect pr=local(g_layout.hunt_progress);M5.Display.fillRect(pr.x,pr.y,pr.w,pr.h,TFT_BLACK);text(progress,pr.x+pr.w/2,pr.y+pr.h/2,g_snapshot.hunt_active?kGreen:kMuted,1);}
  const Rect start=g_layout.legacy?Rect{790,190,220,58}:local(g_layout.page_controls[3]);
  const Rect cancel=g_layout.legacy?Rect{1020,190,224,58}:local(g_layout.page_controls[4]);
  button(start,g_snapshot.hunt_active?"SCANNING":g_hunt_start_pending?"STARTING...":"START",g_snapshot.hunt_active||g_hunt_start_pending,g_snapshot.hunt_available);
  button(cancel,"CANCEL",false,g_snapshot.hunt_active||g_hunt_start_pending);
  const size_t per_page=static_cast<size_t>(g_layout.hunt_row_count);
  const size_t pages=std::max<size_t>(1,(g_snapshot.hunt_candidate_count+per_page-1)/per_page);
  g_hunt_page=std::min(g_hunt_page,pages-1);
  if (!g_snapshot.hunt_candidate_count) {
    draw_empty_state(g_snapshot.hunt_active?"LISTENING FOR PEAKS...":"NO HUNT RESULTS YET","Select a band, start the scan, then tap a result to listen.",{36,280,1208,250},370,420,g_snapshot.hunt_active?kGreen:TFT_WHITE);
    return;
  }
  if(g_snapshot.hunt_candidate_count>per_page){
    button(local(g_layout.hunt_previous),"<",false,g_hunt_page>0);
    char page_label[48];snprintf(page_label,sizeof(page_label),"PAGE %u / %u  |  %u RESULTS",unsigned(g_hunt_page+1),unsigned(pages),unsigned(g_snapshot.hunt_candidate_count));
    button(local(g_layout.hunt_page),page_label,true);
    button(local(g_layout.hunt_next),">",false,g_hunt_page+1<pages);
  }
  const size_t first=g_hunt_page*per_page;
  for (size_t i = 0; i < std::min<size_t>(g_snapshot.hunt_candidate_count-first, per_page); ++i) {
    const size_t candidate_index=first+i;
    const Candidate& candidate = g_snapshot.hunt_candidates[candidate_index];
    char row[96];
    snprintf(row, sizeof(row), "%u.  %.3f MHz     %+.1f dBFS",
             static_cast<unsigned>(candidate_index + 1),
             static_cast<double>(candidate.frequency_hz) / 1000000.0,
             static_cast<double>(candidate.level_dbfs));
    if(g_layout.legacy)button(36,270+static_cast<int>(i)*52,1208,44,row);
    else button(local(g_layout.rows[i]),row);
  }
}

void draw_hunt_progress() {
  if(g_state.tab()!=Tab::hunt)return;
  const Rect pr=local(g_layout.hunt_progress);M5.Display.fillRect(pr.x,pr.y,pr.w,pr.h,TFT_BLACK);
  char progress[48];snprintf(progress,sizeof(progress),"%u / %u",g_snapshot.hunt_step,g_snapshot.hunt_total);
  text(progress,g_layout.legacy?1170:pr.x+pr.w/2,g_layout.legacy?165:pr.y+pr.h/2,g_snapshot.hunt_active?kGreen:kMuted,g_layout.legacy?2:1);
  button(g_layout.legacy?Rect{790,190,220,58}:local(g_layout.page_controls[3]),g_snapshot.hunt_active?"SCANNING":g_hunt_start_pending?"STARTING...":"START",g_snapshot.hunt_active||g_hunt_start_pending,g_snapshot.hunt_available);
  button(g_layout.legacy?Rect{1020,190,224,58}:local(g_layout.page_controls[4]),"CANCEL",false,g_snapshot.hunt_active||g_hunt_start_pending);
}

void draw_memory() {
  draw_page_title("MEMORY", "Saved frequencies live on the SD card");
  draw_storage_status();
  const Rect save=g_layout.legacy?Rect{980,170,264,58}:Rect{g_layout.page_controls[3].x,g_layout.page_controls[3].y,g_layout.content.w-16,g_layout.page_controls[3].h};
  button(save, "SAVE CURRENT", true,
         g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready);
  const size_t count = g_snapshot.memories ? g_snapshot.memories->size() : 0;
  const size_t per_page=g_layout.legacy?7u:static_cast<size_t>(g_layout.row_count);
  const size_t pages = std::max<size_t>(1, (count + per_page - 1) / per_page);
  g_memory_page = std::min(g_memory_page, pages - 1);
  const Rect prev=g_layout.legacy?Rect{36,170,70,58}:local(g_layout.page_controls[0]);
  const Rect next=g_layout.legacy?Rect{300,170,70,58}:local(g_layout.page_controls[2]);
  button(prev, "<", false, g_memory_page > 0);
  char page[48];
  snprintf(page, sizeof(page), "PAGE %u / %u", static_cast<unsigned>(g_memory_page + 1),
           static_cast<unsigned>(pages));
  text(page, g_layout.legacy?190:g_layout.page_controls[1].x+g_layout.page_controls[1].w/2,
       g_layout.legacy?199:g_layout.page_controls[1].y+g_layout.page_controls[1].h/2,kMuted, g_layout.portrait?1:2);
  button(next, ">", false, g_memory_page + 1 < pages);
  if (!count) {
    draw_empty_state("NO SHORTWAVE MEMORIES","Tune a signal in LIVE, then save the current frequency here.",{36,260,1208,270},355,410);
    return;
  }
  const size_t first = g_memory_page * per_page;
  for (size_t row = 0; row < std::min<size_t>(count - first, per_page); ++row) {
    const size_t i = first + row;
    const Memory* memory = g_snapshot.memories->at(i);
    if (!memory) continue;
    char label[128];
    snprintf(label, sizeof(label), "%u.  %.3f MHz  %-3s  %s",
             static_cast<unsigned>(i + 1),
             static_cast<double>(memory->frequency_hz) / 1000000.0,
             memory->mode, memory->station[0] ? memory->station : "Unlabeled signal");
    const bool writable=g_snapshot.storage_available&&g_snapshot.storage_status==StorageStatus::ready;
    if(g_layout.legacy){int y=250+static_cast<int>(row)*50;button(36,y,780,42,label);
      button(826,y,100,42,memory->favorite?"STAR":"FAV",memory->favorite,writable);
      button(936,y,130,42,"EDIT",false,writable);button(1076,y,168,42,g_delete_memory==i?"CONFIRM":"DELETE",false,writable);}
    else {button(local(g_layout.row_actions[row][0]),label);button(local(g_layout.row_actions[row][1]),memory->favorite?"STAR":"FAV",memory->favorite,writable);button(local(g_layout.row_actions[row][2]),"EDIT",false,writable);button(local(g_layout.row_actions[row][3]),g_layout.row_actions[row][3].w<88?(g_delete_memory==i?"OK?":"DEL"):(g_delete_memory==i?"CONFIRM":"DELETE"),false,writable);}
  }
}

void draw_logbook() {
  draw_page_title("LOGBOOK", "Reception history and community-friendly exports");
  draw_storage_status();
  const Rect log_current=g_layout.legacy?Rect{720,170,250,58}:local(g_layout.page_controls[3]);
  const Rect export_rect=g_layout.legacy?Rect{984,170,260,58}:local(g_layout.page_controls[4]);
  button(log_current, "LOG CURRENT", true,
         g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready);
  button(export_rect, "EXPORT CSV + ADIF", false,
         g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready);
  const size_t count = g_snapshot.logs ? g_snapshot.logs->size() : 0;
  const size_t per_page=g_layout.legacy?7u:static_cast<size_t>(g_layout.row_count);
  const size_t pages = std::max<size_t>(1, (count + per_page - 1) / per_page);
  g_log_page = std::min(g_log_page, pages - 1);
  const Rect prev=g_layout.legacy?Rect{36,170,70,58}:local(g_layout.page_controls[0]);
  const Rect next=g_layout.legacy?Rect{300,170,70,58}:local(g_layout.page_controls[2]);
  button(prev, "<", false, g_log_page > 0);
  char page[48];
  snprintf(page, sizeof(page), "PAGE %u / %u", static_cast<unsigned>(g_log_page + 1),
           static_cast<unsigned>(pages));
  text(page, g_layout.legacy?190:g_layout.page_controls[1].x+g_layout.page_controls[1].w/2,
       g_layout.legacy?199:g_layout.page_controls[1].y+g_layout.page_controls[1].h/2,kMuted, g_layout.portrait?1:2);
  button(next, ">", false, g_log_page + 1 < pages);
  if (!count) {
    draw_empty_state("NO RECEPTION LOGS","Log what you actually heard; station identity may remain unknown.",{36,260,1208,270},355,410);
    return;
  }
  const size_t first = g_log_page * per_page;
  for (size_t row_index = 0; row_index < std::min<size_t>(count - first, per_page);
       ++row_index) {
    const LogEntry* entry = g_snapshot.logs->at(first + row_index);
    if (!entry) continue;
    char row[144];
    snprintf(row, sizeof(row), "%.3f MHz  %-3s  %+.1f dBFS  %s",
             static_cast<double>(entry->frequency_hz) / 1000000.0,
             entry->mode, static_cast<double>(entry->signal_dbfs),
             entry->station[0] ? entry->station : "Unidentified reception");
    const size_t index = first + row_index;
    const bool writable=g_snapshot.storage_available&&g_snapshot.storage_status==StorageStatus::ready;
    if(g_layout.legacy){int y=250+static_cast<int>(row_index)*50;button(36,y,880,42,row);button(926,y,140,42,"EDIT",false,writable);button(1076,y,168,42,g_delete_log==index?"CONFIRM":"DELETE",false,writable);}
    else {button(local(g_layout.row_actions[row_index][0]),row);button(local(g_layout.row_actions[row_index][2]),"EDIT",false,writable);button(local(g_layout.row_actions[row_index][3]),g_layout.row_actions[row_index][3].w<88?(g_delete_log==index?"OK?":"DEL"):(g_delete_log==index?"CONFIRM":"DELETE"),false,writable);}
  }
}

void draw_keypad() {
  if (g_self_check_running) return;
  const freq_keypad::Rect area{g_viewport.x,g_viewport.y,g_viewport.w,g_viewport.h};
  freq_keypad::draw(g_layout.legacy?93:g_viewport.y, TFT_BLACK, "ENTER SHORTWAVE FREQUENCY", "0.024 - 30.000", "MHz",
                    g_entry,g_layout.legacy?nullptr:&area);
}

void draw_text_editor() {
  if(!g_layout.legacy)
    (void)text_editor::set_viewport({g_viewport.x,g_viewport.y,g_viewport.w,g_viewport.h});
  text_editor::draw();
}

uint16_t waterfall_color(float level) {
  return waterfall_style::color565(waterfall_style::Screen::shortwave, level);
}

}  // namespace

bool set_viewport(layout::Rect safe) {
  const layout::Layout next=layout::make_layout(safe);
  if(!next.valid)return false;
  if(!audio_header::set_viewport(safe))return false;
  const bool dimensions_changed=safe.w!=g_viewport.w||safe.h!=g_viewport.h||
      safe.x!=g_viewport.x||safe.y!=g_viewport.y;
  install_geometry(next);
  if(dimensions_changed)g_static_dirty=true;
  return true;
}

layout::Rect viewport() { return g_viewport; }

Action handle_control(Control control,int32_t value) {
  if(!g_active)return {};
  switch(control){
    case Control::tune_frequency:return value>=24000&&value<=30000000?Action{ActionKind::tune_hz,value}:Action{};
    case Control::step_down:return {ActionKind::step_down};
    case Control::step_up:return {ActionKind::step_up};
    case Control::step_cycle:return {ActionKind::step_cycle};
    case Control::filter_down:return ssb_mode()?Action{}:Action{ActionKind::filter_down};
    case Control::filter_up:return ssb_mode()?Action{}:Action{ActionKind::filter_up};
    case Control::filter_cycle:return ssb_mode()?Action{}:Action{ActionKind::filter_cycle};
    case Control::span_down:return {ActionKind::span_down};
    case Control::span_up:return {ActionKind::span_up};
    case Control::sound:return {ActionKind::sound_toggle};
    case Control::volume_down:return {ActionKind::volume_down};
    case Control::volume_up:return {ActionKind::volume_up};
    case Control::mode_cycle:return {ActionKind::mode_cycle};
    case Control::audio_clean:return g_snapshot.audio_dsp_available?Action{ActionKind::clean_audio}:Action{};
    case Control::audio_boost:return g_snapshot.audio_dsp_available?Action{ActionKind::audio_boost,!g_snapshot.controls.audio_boost}:Action{};
    case Control::noise_reduction:return g_snapshot.audio_dsp_available?Action{ActionKind::noise_reduction_cycle}:Action{};
    case Control::auto_notch:return g_snapshot.audio_dsp_available?Action{ActionKind::auto_notch_toggle}:Action{};
    case Control::squelch:return g_snapshot.audio_dsp_available?Action{ActionKind::squelch_cycle}:Action{};
    case Control::hunt_start:return g_snapshot.hunt_available?Action{ActionKind::hunt_start,static_cast<int32_t>(g_hunt_band)}:Action{};
    case Control::hunt_cancel:return g_snapshot.hunt_available&&g_snapshot.hunt_active?Action{ActionKind::hunt_cancel}:Action{};
    case Control::open_frequency:return {ActionKind::open_frequency};
    case Control::open_settings:return {ActionKind::open_settings};
    case Control::home:return {ActionKind::exit_home};
  }
  return {};
}

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_saved_frequency = snapshot.frequency_hz;
  g_filter_dragging = false;
  g_tuner_drawer_open = false;
  g_edit_memory = SIZE_MAX;
  g_edit_log = SIZE_MAX;
  g_memory_page = 0;
  g_log_page = 0;
  g_hunt_page = 0;
  g_on_air_page = 0;
  g_delete_memory = SIZE_MAX;
  g_delete_log = SIZE_MAX;
  g_active = true;
  g_static_dirty = true;
  g_state.close_modal();
  g_state.select_tab(Tab::live);
  if (const BroadcastBand* current = band_for(snapshot.frequency_hz)) {
    for (size_t i = 0; i < band_count(); ++i)
      if (band(i) == current) g_hunt_band = i;
  }
  g_entry[0] = '\0';
  draw();
}

void leave() {
  M5.Display.clearScrollRect();
  g_filter_dragging = false;
  g_tuner_drawer_open = false;
  g_delete_memory = SIZE_MAX;
  g_delete_log = SIZE_MAX;
  g_active = false;
}

void draw() {
  if (!g_active || g_self_check_running) return;
  if(g_drawn_tab!=g_state.tab()||g_drawn_modal!=g_state.modal()||g_drawn_drawer!=g_tuner_drawer_open)
    g_static_dirty=true;
  if (g_static_dirty) { draw_static(); g_static_dirty = false; }
  g_drawn_tab=g_state.tab();g_drawn_modal=g_state.modal();g_drawn_drawer=g_tuner_drawer_open;
  if (g_state.modal() == Modal::frequency) {
    draw_keypad();
    return;
  }
  if (g_state.modal() != Modal::none) {
    draw_text_editor();
    return;
  }
  switch (g_state.tab()) {
    case Tab::live:
      draw_frequency();
      draw_status();
      draw_quick_controls();
      draw_tuner_drawer();
      break;
    case Tab::on_air: draw_on_air(); break;
    case Tab::hunt: draw_hunt(); break;
    case Tab::memory: draw_memory(); break;
    case Tab::logbook: draw_logbook(); break;
  }
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  const bool frequency_changed = snapshot.frequency_hz != g_snapshot.frequency_hz;
  const bool span_changed = snapshot.span_hz != g_snapshot.span_hz;
  const bool mode_changed = std::strcmp(snapshot.mode, g_snapshot.mode) != 0 ||
      snapshot.mode_auto != g_snapshot.mode_auto || snapshot.clarifier_hz != g_snapshot.clarifier_hz;
  const bool hunt_candidates_changed=snapshot.hunt_candidate_count!=g_snapshot.hunt_candidate_count;
  const bool pending_timeout=g_hunt_start_pending&&static_cast<uint32_t>(millis()-g_hunt_pending_since)>2500;
  const bool hunt_progress_changed=snapshot.hunt_active!=g_snapshot.hunt_active||
      snapshot.hunt_step!=g_snapshot.hunt_step||snapshot.hunt_total!=g_snapshot.hunt_total||
      snapshot.hunt_available!=g_snapshot.hunt_available||pending_timeout;
  const bool page_changed =
      frequency_changed || snapshot.utc_valid != g_snapshot.utc_valid ||
      snapshot.utc_minute != g_snapshot.utc_minute ||
      snapshot.utc_weekday != g_snapshot.utc_weekday ||
      snapshot.storage_status != g_snapshot.storage_status ||
      snapshot.memories != g_snapshot.memories || snapshot.logs != g_snapshot.logs ||
      snapshot.memory_count != g_snapshot.memory_count ||
      snapshot.log_count != g_snapshot.log_count ||
      hunt_candidates_changed ||
      snapshot.audio_dsp_available != g_snapshot.audio_dsp_available ||
      snapshot.storage_available != g_snapshot.storage_available;
  const bool controls_changed =
      snapshot.filter_bandwidth_hz != g_snapshot.filter_bandwidth_hz ||
      snapshot.controls.route != g_snapshot.controls.route ||
      snapshot.controls.capabilities.rf_gain != g_snapshot.controls.capabilities.rf_gain ||
      snapshot.controls.capabilities.tuner_agc != g_snapshot.controls.capabilities.tuner_agc ||
      snapshot.controls.capabilities.rtl_agc != g_snapshot.controls.capabilities.rtl_agc ||
      snapshot.controls.tuner_agc != g_snapshot.controls.tuner_agc ||
      snapshot.controls.rtl_agc != g_snapshot.controls.rtl_agc ||
      snapshot.controls.audio_boost != g_snapshot.controls.audio_boost ||
      snapshot.controls.volume != g_snapshot.controls.volume ||
      snapshot.controls.gain_tenth_db != g_snapshot.controls.gain_tenth_db ||
      snapshot.gain_step_count != g_snapshot.gain_step_count ||
      snapshot.dsp.noise_reduction != g_snapshot.dsp.noise_reduction ||
      snapshot.dsp.auto_notch != g_snapshot.dsp.auto_notch ||
      snapshot.dsp.squelch != g_snapshot.dsp.squelch ||
      snapshot.dsp.squelch_dbfs != g_snapshot.dsp.squelch_dbfs ||
      snapshot.dsp_metrics.squelch_open != g_snapshot.dsp_metrics.squelch_open ||
      snapshot.dsp_metrics.notch_active != g_snapshot.dsp_metrics.notch_active ||
      snapshot.dsp_metrics.notch_hz != g_snapshot.dsp_metrics.notch_hz ||
      snapshot.audio_dsp_available != g_snapshot.audio_dsp_available;
  const bool quick_changed = controls_changed || mode_changed ||
      snapshot.step_hz != g_snapshot.step_hz;
  g_snapshot = snapshot;
  if(snapshot.hunt_active||pending_timeout)g_hunt_start_pending=false;
  g_saved_frequency = snapshot.frequency_hz;
  if (!g_state.background_redraw_allowed()) return;
  if (g_state.tab() != Tab::live) {
    switch (g_state.tab()) {
      case Tab::on_air: if(page_changed)draw_on_air(false); break;
      case Tab::hunt:
        if(hunt_candidates_changed)draw_hunt();
        else if(hunt_progress_changed)draw_hunt_progress();
        break;
      case Tab::memory: if(page_changed)draw_memory(); break;
      case Tab::logbook: if(page_changed)draw_logbook(); break;
      case Tab::live: break;
    }
    return;
  }
  if (frequency_changed || mode_changed) draw_frequency();
  draw_status();
  if (quick_changed) draw_quick_controls();
  if (span_changed) draw_zoom_controls();
  if (frequency_changed || span_changed) draw_spectrum_scale();
  if (controls_changed || (quick_changed && g_tuner_drawer_open))
    draw_tuner_drawer();
}

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                   float floor) {
  if (!spectrum_active() || !levels || visible_bins < 2) return;
  const int width = spectrum_width();
  // The trace is drawn off-screen and pushed in one go: erasing and redrawing the screen area every frame
  // flickered and cost time. The sprite follows the width, which changes when the tuner drawer opens.
  const int cw = width - 2;
  const int ch = kSpectrumPlotH - 1;
  static scope::Trace trace;
  M5Canvas* canvas = trace.begin(cw, ch, TFT_BLACK);
  // No memory for the trace sprite: leave the frame alone rather than scroll the waterfall under a stale spectrum.
  if (canvas == nullptr) return;
  int last_x = 0;
  int last_y = ch - 2;
  for (size_t i = 0; i < static_cast<size_t>(width); ++i) {
    const float level = spectrum::peak_for_pixel(
        levels, first_bin, visible_bins, i, width);
    const float normalized = std::clamp((level - floor) / 48.0f,
                                        0.0f, 1.0f);
    const int x = static_cast<int>(i) - 1;
    const int y = ch - 2 - static_cast<int>(normalized * (kSpectrumPlotH - 4));
    if (canvas != nullptr && i && x >= 0 && x < cw) canvas->drawLine(last_x, last_y, x, y, kGreen);
    last_x = x;
    last_y = y;
  }
  if (canvas != nullptr) {
    const int center = cw / 2;
    const int half_filter = filter_half_width();
    canvas->drawFastVLine(center, 0, ch, kCyan);
    canvas->drawFastVLine(center - half_filter, 0, ch, kYellow);
    canvas->drawFastVLine(center + half_filter, 0, ch, kYellow);
    scope::draw_style_chips(canvas, cw, waterfall_style::Screen::shortwave);
    canvas->pushSprite(kSpectrumX + 1, kSpectrumY + 1);
  }
  const int wf_width=g_layout.waterfall.w;
  for(int i=0;i<wf_width;++i){const float level=spectrum::peak_for_pixel(levels,first_bin,visible_bins,static_cast<size_t>(i),static_cast<size_t>(wf_width));const float norm=std::clamp((level-floor)/48.0f,0.0f,1.0f);g_waterfall_row[i]=waterfall_color(norm);}
  scope::scroll_waterfall(kWaterfallX, kWaterfallY + kWaterfallH - 2, wf_width, g_waterfall_row,
                          waterfall_style::rows_due(waterfall_style::Screen::shortwave, millis()));
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (spectrum_active() &&
      scope::style_chip_tap(x, y, kSpectrumX + 1, kSpectrumY + 1, spectrum_width() - 2, waterfall_style::Screen::shortwave))
    return {};
  if (g_state.modal() != Modal::none && g_state.modal() != Modal::frequency) {
    const Modal modal = g_state.modal();
    const auto result = text_editor::handle_touch(x, y);
    if (result == text_editor::Result::cancelled) {
      g_state.close_modal();
      draw();
      return {};
    }
    if (result != text_editor::Result::accepted) return {};
    if (modal == Modal::memory_label) {
      snprintf(g_pending_memory_label, sizeof(g_pending_memory_label), "%s",
               text_editor::value());
      g_state.open(Modal::memory_notes);
      text_editor::begin("MEMORY NOTES", g_pending_memory_notes,
                         sizeof(g_pending_memory_notes) - 1, false,
                         g_edit_memory == SIZE_MAX ? "SAVE" : "UPDATE");
      draw_text_editor();
      return {};
    }
    if (modal == Modal::memory_notes) {
      snprintf(g_pending_memory_notes, sizeof(g_pending_memory_notes), "%s",
               text_editor::value());
      g_state.close_modal();
      draw();
      return {g_edit_memory == SIZE_MAX ? ActionKind::save_memory
                                        : ActionKind::update_memory,
              g_edit_memory == SIZE_MAX ? 0 : static_cast<int32_t>(g_edit_memory)};
    }
    if (modal == Modal::log_antenna) {
      snprintf(g_pending_log_antenna, sizeof(g_pending_log_antenna), "%s",
               text_editor::value());
      g_state.open(Modal::log_notes);
      text_editor::begin("RECEPTION NOTES", g_pending_log_notes,
                         sizeof(g_pending_log_notes) - 1, false,
                         g_edit_log == SIZE_MAX ? "LOG" : "UPDATE");
      draw_text_editor();
      return {};
    }
    if (modal == Modal::log_notes) {
      snprintf(g_pending_log_notes, sizeof(g_pending_log_notes), "%s",
               text_editor::value());
      g_state.close_modal();
      draw();
      return {g_edit_log == SIZE_MAX ? ActionKind::save_log
                                     : ActionKind::update_log,
              g_edit_log == SIZE_MAX ? 0 : static_cast<int32_t>(g_edit_log)};
    }
    return {};
  }
  if (g_state.modal() == Modal::frequency) {
    const auto result = freq_keypad::handle_touch(x, y, g_entry, sizeof(g_entry));
    if (result == freq_keypad::Result::cancelled) {
      g_state.close_modal();
      g_entry[0] = '\0';
      draw();
      return {};
    }
    if (result == freq_keypad::Result::submitted) {
      char* end = nullptr;
      const double mhz = strtod(g_entry, &end);
      if (end != g_entry && *end == '\0' && mhz >= 0.024 && mhz <= 30.0) {
        g_state.close_modal();
        const uint32_t hz = static_cast<uint32_t>(llround(mhz * 1000000.0));
        g_entry[0] = '\0';
        draw();
        return {ActionKind::tune_hz, static_cast<int32_t>(hz)};
      }
    }
    return {};
  }
  if (audio_header::home_hit(x, y)) return {ActionKind::exit_home};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_settings};
  int tab_index=-1;
  for(int i=0;i<5;++i) if(hit(x,y,local(g_layout.tab[i]))) { tab_index=i; break; }
  if (tab_index >= 0) {
    const bool cancel_hunt = g_state.select_tab(static_cast<Tab>(tab_index));
    if(tab_index==static_cast<int>(Tab::hunt))g_hunt_page=0;
    if(tab_index==static_cast<int>(Tab::on_air))g_on_air_page=0;
    g_static_dirty=true;
    draw();
    return cancel_hunt ? Action{ActionKind::hunt_cancel} : Action{};
  }

  if (g_state.tab() == Tab::on_air) {
    if (!g_snapshot.utc_valid) return {};
    const size_t max_rows=static_cast<size_t>(g_layout.onair_row_count);
    size_t matches=0;for(size_t i=0;i<station_count();++i){const StationCard* s=station_at(i);if(s&&schedule_matches(*s,g_snapshot.frequency_hz,g_snapshot.utc_minute,g_snapshot.utc_weekday))++matches;}
    const size_t pages=std::max<size_t>(1,(matches+max_rows-1)/max_rows);g_on_air_page=std::min(g_on_air_page,pages-1);
    const Rect prev=local(g_layout.hunt_previous),next=local(g_layout.hunt_next);
    if(matches>max_rows&&hit(x,y,prev)&&g_on_air_page>0){--g_on_air_page;draw();return {};}
    if(matches>max_rows&&hit(x,y,next)&&g_on_air_page+1<pages){++g_on_air_page;draw();return {};}
    const size_t first=g_on_air_page*max_rows;size_t row=0,match_index=0;
    for (size_t i = 0; i < station_count() && row < max_rows; ++i) {
      const StationCard* station = station_at(i);
      if (!station || !schedule_matches(*station, g_snapshot.frequency_hz,
                                        g_snapshot.utc_minute,
                                        g_snapshot.utc_weekday))
        continue;
      if(match_index++<first)continue;
      const Rect target=g_layout.legacy?Rect{36,220+static_cast<int>(row)*58,1208,50}:local(g_layout.rows[row]);
      if (hit(x, y, target))
        return {ActionKind::tune_hz, static_cast<int32_t>(station->frequency_hz)};
      ++row;
    }
    return {};
  }

  if (g_state.tab() == Tab::hunt) {
    const Rect band_prev=g_layout.legacy?Rect{36,190,80,58}:local(g_layout.page_controls[0]);
    const Rect band_next=g_layout.legacy?Rect{676,190,80,58}:local(g_layout.page_controls[2]);
    const Rect start=g_layout.legacy?Rect{790,190,220,58}:local(g_layout.page_controls[3]);
    const Rect cancel=g_layout.legacy?Rect{1020,190,224,58}:local(g_layout.page_controls[4]);
    const Rect hunt_prev=local(g_layout.hunt_previous),hunt_next=local(g_layout.hunt_next);
    const size_t per_page=static_cast<size_t>(g_layout.hunt_row_count);
    const size_t pages=std::max<size_t>(1,(g_snapshot.hunt_candidate_count+per_page-1)/per_page);
    if(hit(x,y,hunt_prev)&&g_hunt_page>0){--g_hunt_page;draw();return {};}
    if(hit(x,y,hunt_next)&&g_hunt_page+1<pages){++g_hunt_page;draw();return {};}
    if (g_snapshot.hunt_available&&hit(x, y, band_prev)) {
      g_hunt_band = (g_hunt_band + band_count() - 1) % band_count();
      draw();
      return {};
    }
    if (g_snapshot.hunt_available&&hit(x, y, band_next)) {
      g_hunt_band = (g_hunt_band + 1) % band_count();
      draw();
      return {};
    }
    if (g_snapshot.hunt_available && hit(x, y, start) && !g_snapshot.hunt_active){
      g_hunt_start_pending=true;g_hunt_pending_since=millis();draw_hunt_progress();
      return {ActionKind::hunt_start, static_cast<int32_t>(g_hunt_band)};
    }
    if (hit(x, y, cancel) && (g_snapshot.hunt_active||g_hunt_start_pending)){
      g_hunt_start_pending=false;draw_hunt_progress();
      return {ActionKind::hunt_cancel};
    }
    for (size_t i = 0; !g_snapshot.hunt_active &&
                       i < std::min<size_t>(g_snapshot.hunt_candidate_count-g_hunt_page*per_page, per_page); ++i) {
      const size_t candidate_index=g_hunt_page*per_page+i;
      const Rect target=g_layout.legacy?Rect{36,270+static_cast<int>(i)*52,1208,44}:local(g_layout.rows[i]);
      if (hit(x,y,target))
        return {ActionKind::tune_hz,
                static_cast<int32_t>(g_snapshot.hunt_candidates[candidate_index].frequency_hz)};
    }
    return {};
  }

  if (g_state.tab() == Tab::memory) {
    const size_t count = g_snapshot.memories ? g_snapshot.memories->size() : 0;
    const size_t per_page=g_layout.legacy?7u:static_cast<size_t>(g_layout.row_count);
    const size_t pages = std::max<size_t>(1, (count + per_page - 1) / per_page);
    g_memory_page = std::min(g_memory_page, pages - 1);
    const Rect prev=g_layout.legacy?Rect{36,170,70,58}:local(g_layout.page_controls[0]);
    const Rect next=g_layout.legacy?Rect{300,170,70,58}:local(g_layout.page_controls[2]);
    const Rect save=g_layout.legacy?Rect{980,170,264,58}:Rect{g_layout.page_controls[3].x,g_layout.page_controls[3].y,g_layout.content.w-16,g_layout.page_controls[3].h};
    if (hit(x, y, prev) && g_memory_page > 0) {
      --g_memory_page; g_delete_memory = SIZE_MAX; draw(); return {};
    }
    if (hit(x, y, next) && g_memory_page + 1 < pages) {
      ++g_memory_page; g_delete_memory = SIZE_MAX; draw(); return {};
    }
    if (g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready &&
        hit(x, y, save)) {
      g_edit_memory = SIZE_MAX;
      g_pending_memory_label[0] = '\0';
      g_pending_memory_notes[0] = '\0';
      g_state.open(Modal::memory_label);
      text_editor::begin("MEMORY LABEL", "", sizeof(g_pending_memory_label) - 1,
                         false, "SAVE");
      draw_text_editor();
      return {};
    }
    const size_t first = g_memory_page * per_page;
    for (size_t row = 0; row < std::min<size_t>(count - first, per_page); ++row) {
      const size_t i = first + row;
      const Memory* memory = g_snapshot.memories->at(i);
      if (!memory) continue;
      const Rect tune=g_layout.legacy?Rect{36,250+static_cast<int>(row)*50,780,42}:local(g_layout.row_actions[row][0]);
      const Rect fav=g_layout.legacy?Rect{826,250+static_cast<int>(row)*50,100,42}:local(g_layout.row_actions[row][1]);
      const Rect edit=g_layout.legacy?Rect{936,250+static_cast<int>(row)*50,130,42}:local(g_layout.row_actions[row][2]);
      const Rect del=g_layout.legacy?Rect{1076,250+static_cast<int>(row)*50,168,42}:local(g_layout.row_actions[row][3]);
      if (hit(x, y, tune))
        return {ActionKind::tune_hz, static_cast<int32_t>(memory->frequency_hz)};
      if (g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready && hit(x, y, fav))
        return {ActionKind::favorite_memory, static_cast<int32_t>(i)};
      if (g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready && hit(x, y, edit)) {
        g_edit_memory = i;
        snprintf(g_pending_memory_label, sizeof(g_pending_memory_label), "%s",
                 memory->station);
        snprintf(g_pending_memory_notes, sizeof(g_pending_memory_notes), "%s",
                 memory->notes);
        g_state.open(Modal::memory_label);
        text_editor::begin("MEMORY LABEL", g_pending_memory_label,
                           sizeof(g_pending_memory_label) - 1, false, "NEXT");
        draw_text_editor();
        return {};
      }
      if (g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready && hit(x, y, del)) {
        if (g_delete_memory == i) {
          g_delete_memory = SIZE_MAX;
          return {ActionKind::delete_memory, static_cast<int32_t>(i)};
        }
        g_delete_memory = i;
        draw();
        return {};
      }
    }
    g_delete_memory = SIZE_MAX;
    return {};
  }

  if (g_state.tab() == Tab::logbook) {
    const size_t count = g_snapshot.logs ? g_snapshot.logs->size() : 0;
    const size_t per_page=g_layout.legacy?7u:static_cast<size_t>(g_layout.row_count);
    const size_t pages = std::max<size_t>(1, (count + per_page - 1) / per_page);
    g_log_page = std::min(g_log_page, pages - 1);
    const Rect prev=g_layout.legacy?Rect{36,170,70,58}:local(g_layout.page_controls[0]);
    const Rect next=g_layout.legacy?Rect{300,170,70,58}:local(g_layout.page_controls[2]);
    const Rect log_current=g_layout.legacy?Rect{720,170,250,58}:local(g_layout.page_controls[3]);
    const Rect export_rect=g_layout.legacy?Rect{984,170,260,58}:local(g_layout.page_controls[4]);
    if (hit(x, y, prev) && g_log_page > 0) {
      --g_log_page; g_delete_log = SIZE_MAX; draw(); return {};
    }
    if (hit(x, y, next) && g_log_page + 1 < pages) {
      ++g_log_page; g_delete_log = SIZE_MAX; draw(); return {};
    }
    if (g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready &&
        hit(x, y, log_current)) {
      g_edit_log = SIZE_MAX;
      g_pending_log_antenna[0] = '\0';
      g_pending_log_notes[0] = '\0';
      g_state.open(Modal::log_antenna);
      text_editor::begin("ANTENNA USED", "", sizeof(g_pending_log_antenna) - 1,
                         false, "NEXT");
      draw_text_editor();
      return {};
    }
    if (g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready &&
        hit(x, y, export_rect))
      return {ActionKind::export_log};
    const size_t first = g_log_page * per_page;
    for (size_t row = 0; row < std::min<size_t>(count - first, per_page); ++row) {
      const size_t i = first + row;
      const LogEntry* entry = g_snapshot.logs->at(i);
      if (!entry) continue;
      const Rect edit=g_layout.legacy?Rect{926,250+static_cast<int>(row)*50,140,42}:local(g_layout.row_actions[row][2]);
      const Rect del=g_layout.legacy?Rect{1076,250+static_cast<int>(row)*50,168,42}:local(g_layout.row_actions[row][3]);
      if (g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready && hit(x, y, edit)) {
        g_edit_log = i;
        snprintf(g_pending_log_antenna, sizeof(g_pending_log_antenna), "%s",
                 entry->antenna);
        snprintf(g_pending_log_notes, sizeof(g_pending_log_notes), "%s",
                 entry->notes);
        g_state.open(Modal::log_antenna);
        text_editor::begin("ANTENNA USED", g_pending_log_antenna,
                           sizeof(g_pending_log_antenna) - 1, false, "NEXT");
        draw_text_editor();
        return {};
      }
      if (g_snapshot.storage_available && g_snapshot.storage_status == StorageStatus::ready && hit(x, y, del)) {
        if (g_delete_log == i) {
          g_delete_log = SIZE_MAX;
          return {ActionKind::delete_log, static_cast<int32_t>(i)};
        }
        g_delete_log = i;
        draw();
        return {};
      }
    }
    g_delete_log = SIZE_MAX;
    return {};
  }

  if (g_tuner_drawer_open) {
    if (hit(x, y, kDrawerClose)) {
      g_tuner_drawer_open = false;
      draw();
      return {};
    }
    if (hit(x, y, kTunerAgc) &&
        receiver_controls::action(receiver_controls::Control::tuner_agc,
                                  g_snapshot.controls).kind !=
            receiver_controls::ActionKind::none)
      return {ActionKind::gain_auto};
    if (hit(x, y, kRtlAgc) &&
        receiver_controls::action(receiver_controls::Control::rtl_agc,
                                  g_snapshot.controls).kind !=
            receiver_controls::ActionKind::none)
      return {ActionKind::rtl_agc, !g_snapshot.controls.rtl_agc};
    if (hit(x, y, kDrawer)) return {};
  }

  const int width = spectrum_width();
  const Rect frequency_plus=g_layout.legacy?Rect{kSpectrumX+width-82,kFrequencyPlus.y,kFrequencyPlus.w,kFrequencyPlus.h}:local(g_layout.frequency_plus);
  const Rect frequency_entry=g_layout.legacy?Rect{kFrequencyEntry.x,kFrequencyEntry.y,width-192,kFrequencyEntry.h}:local(g_layout.frequency_value);
  if (ssb_mode() && hit(x, y, kClarifierDown)) return {ActionKind::clarifier_down};
  if (ssb_mode() && hit(x, y, kClarifierUp)) return {ActionKind::clarifier_up};
  if (hit(x, y, kFrequencyMinus)) return {ActionKind::step_down};
  if (hit(x, y, frequency_plus)) return {ActionKind::step_up};
  if (hit(x, y, frequency_entry)) {
    g_state.open(Modal::frequency);
    g_entry[0] = '\0';
    draw();
    return {};
  }
  if (hit(x, y, kStep)) return {ActionKind::step_cycle};
  // The SSB passband is fixed, so the AM width controls do nothing there.
  if (ssb_mode() && (hit(x, y, kFilterDown) || hit(x, y, kFilterValue) || hit(x, y, kFilterUp))) return {};
  if (hit(x, y, kFilterDown)) return {ActionKind::filter_down};
  if (hit(x, y, kFilterValue)) return {ActionKind::filter_cycle};
  if (hit(x, y, kFilterUp)) return {ActionKind::filter_up};
  if (hit(x, y, kMode)) return {ActionKind::mode_cycle};
  if (g_snapshot.audio_dsp_available && hit(x, y, kClean)) return {ActionKind::clean_audio};
  if (g_snapshot.audio_dsp_available && hit(x, y, kBoost))
    return {ActionKind::audio_boost, !g_snapshot.controls.audio_boost};
  if (g_snapshot.audio_dsp_available && hit(x, y, kNoiseReduction))
    return {ActionKind::noise_reduction_cycle};
  if (g_snapshot.audio_dsp_available && hit(x, y, kNotch))
    return {ActionKind::auto_notch_toggle};
  if (g_snapshot.audio_dsp_available && hit(x, y, kSquelch)) return {ActionKind::squelch_cycle};
  if (hit(x, y, kTuner)) {
    g_tuner_drawer_open = true;
    draw();
    return {};
  }
  if (hit(x, y, g_layout.legacy?Rect{kSpectrumX,kZoomY,160,34}:local(g_layout.zoom_minus)))
    return {ActionKind::span_down};
  if (hit(x, y, g_layout.legacy?Rect{kSpectrumX+width-160,kZoomY,160,34}:local(g_layout.zoom_plus)))
    return {ActionKind::span_up};
  const bool on_plot=hit(x,y,kSpectrumX,kSpectrumY,width,kSpectrumPlotH);
  const bool on_waterfall=hit(x,y,kWaterfallX,kWaterfallY,g_layout.waterfall.w,kWaterfallH);
  if (on_plot||on_waterfall) {
    const int chart_x=on_waterfall?kWaterfallX:kSpectrumX;
    const int chart_w=on_waterfall?g_layout.waterfall.w:width;
    const int64_t offset = static_cast<int64_t>(x-(chart_x+chart_w/2))*
        g_snapshot.span_hz/chart_w;
    const int64_t selected = static_cast<int64_t>(g_snapshot.frequency_hz) + offset;
    return {ActionKind::tune_hz, static_cast<int32_t>(
        std::clamp<int64_t>(selected, 24000, 30000000))};
  }
  return {};
}

Action handle_gain_drag(int32_t x, int32_t y) {
  if (!g_active || !g_tuner_drawer_open || !g_state.spectrum_allowed() ||
      !hit(x, y, kGainX - 16, kGainY - 20, kGainW + 32, 62) ||
      g_snapshot.gain_step_count == 0 ||
      receiver_controls::item(receiver_controls::Control::rf_gain,
                              g_snapshot.controls).availability !=
          receiver_controls::Availability::enabled)
    return {};
  const int clamped = std::clamp<int32_t>(x, kGainX, kGainX + kGainW);
  const size_t index = static_cast<size_t>(clamped - kGainX) *
                       (g_snapshot.gain_step_count - 1) / kGainW;
  return {ActionKind::gain_tenth_db, g_snapshot.gain_steps_tenth_db[index]};
}

Action handle_filter_drag(int32_t x, int32_t y, bool pressed) {
  if (!pressed) {
    g_filter_dragging = false;
    return {};
  }
  if (!g_active || !g_state.spectrum_allowed() || ssb_mode()) return {};
  const int width = spectrum_width();
  const int center = kSpectrumX + width / 2;
  if (!g_filter_dragging) {
    if (!hit(x, y, kSpectrumX, kSpectrumY, width, kSpectrumPlotH)) return {};
    const int half_filter = filter_half_width();
    if (std::min(std::abs(x - (center - half_filter)),
                 std::abs(x - (center + half_filter))) > 18)
      return {};
    g_filter_dragging = true;
  }
  uint32_t bandwidth = static_cast<uint32_t>(
      2ull * static_cast<uint64_t>(std::abs(x - center)) *
      (g_snapshot.span_hz ? g_snapshot.span_hz : 1u) / width);
  bandwidth = std::clamp<uint32_t>((bandwidth / 1000u) * 1000u, 3000u, 30000u);
  return {ActionKind::filter_bandwidth_hz, static_cast<int32_t>(bandwidth)};
}

bool spectrum_contains(int32_t x, int32_t y) {
  const int width = spectrum_width();
  return g_active && g_state.spectrum_allowed() &&
         (hit(x, y, kSpectrumX, kSpectrumY, width, kSpectrumPlotH) ||
          hit(x, y, kWaterfallX, kWaterfallY, g_layout.waterfall.w, kWaterfallH));
}

bool active() { return g_active; }
bool keypad_open() { return g_active && g_state.modal() == Modal::frequency; }

void begin_frequency_entry() {
  if (!g_active || g_state.modal() != Modal::none) return;
  g_state.open(Modal::frequency);
  g_entry[0] = '\0';
  draw();
}

bool spectrum_active() { return g_active && g_state.spectrum_allowed(); }
uint32_t saved_frequency() { return g_saved_frequency; }
void note_tuned(uint32_t frequency_hz) { g_saved_frequency = frequency_hz; }
const char* pending_memory_label() { return g_pending_memory_label; }
const char* pending_memory_notes() { return g_pending_memory_notes; }
const char* pending_log_antenna() { return g_pending_log_antenna; }
const char* pending_log_notes() { return g_pending_log_notes; }

bool dashboard_self_check() {
  const Snapshot saved = g_snapshot;
  const bool was_active = g_active;
  const bool was_filter_dragging = g_filter_dragging;
  const bool was_drawer_open = g_tuner_drawer_open;
  const Modal saved_modal = g_state.modal();
  const Tab saved_tab = g_state.tab();
  const size_t saved_memory_page = g_memory_page;
  char saved_entry[sizeof(g_entry)];
  memcpy(saved_entry, g_entry, sizeof(g_entry));
  g_self_check_running = true;
  Snapshot test{};
  test.controls.route = ReceiverRoute::hf_upconverter;
  test.controls.capabilities = {true, true, true, false};
  test.gain_steps_tenth_db[0] = 0;
  test.gain_steps_tenth_db[1] = 297;
  test.gain_steps_tenth_db[2] = 496;
  test.gain_step_count = 3;
  test.span_hz = 480000;
  test.filter_bandwidth_hz = 9000;
  test.utc_valid = true;
  test.utc_minute = 600;
  test.utc_weekday = 1;
  g_snapshot = test;
  g_active = true;
  g_tuner_drawer_open = false;
  g_state.close_modal();
  g_state.select_tab(Tab::live);
  const bool controls_ok =
      handle_touch(center_x(kFrequencyMinus), center_y(kFrequencyMinus)).kind ==
          ActionKind::step_down &&
      handle_touch(center_x(kFrequencyPlus), center_y(kFrequencyPlus)).kind ==
          ActionKind::step_up &&
      handle_touch(center_x(kStep), center_y(kStep)).kind == ActionKind::step_cycle &&
      handle_touch(center_x(kFilterDown), center_y(kFilterDown)).kind ==
          ActionKind::filter_down &&
      handle_touch(center_x(kFilterValue), center_y(kFilterValue)).kind ==
          ActionKind::filter_cycle &&
      handle_touch(center_x(kFilterUp), center_y(kFilterUp)).kind ==
          ActionKind::filter_up &&
      handle_touch(center_x(kMode), center_y(kMode)).kind ==
          ActionKind::mode_cycle &&
      handle_touch(center_x(kClean), center_y(kClean)).kind ==
          ActionKind::clean_audio &&
      handle_touch(center_x(kBoost), center_y(kBoost)).kind ==
          ActionKind::audio_boost &&
      handle_touch(center_x(kNoiseReduction), center_y(kNoiseReduction)).kind ==
          ActionKind::noise_reduction_cycle &&
      handle_touch(center_x(kNotch), center_y(kNotch)).kind ==
          ActionKind::auto_notch_toggle &&
      handle_touch(center_x(kSquelch), center_y(kSquelch)).kind ==
          ActionKind::squelch_cycle &&
      handle_touch(kSpectrumX + 40, kZoomY + 10).kind ==
          ActionKind::span_down &&
      handle_touch(kSpectrumX + kSpectrumClosedW - 40, kZoomY + 10).kind ==
          ActionKind::span_up;
  const bool hidden_drawer_ok =
      handle_touch(center_x(kTunerAgc), center_y(kTunerAgc)).kind !=
          ActionKind::gain_auto &&
      handle_gain_drag(kGainX + kGainW, kGainY).kind == ActionKind::none;
  const int filter_edge = kSpectrumX + spectrum_width() / 2 + filter_half_width();
  const bool filter_drag_ok = spectrum_contains(kSpectrumX, kWaterfallY) &&
      handle_filter_drag(filter_edge, kSpectrumY + 20, true).kind ==
          ActionKind::filter_bandwidth_hz &&
      handle_filter_drag(filter_edge + 12, kSpectrumY + 20, true).value > 9000;
  (void)handle_filter_drag(0, 0, false);
  const bool tuner_opens =
      handle_touch(center_x(kTuner), center_y(kTuner)).kind == ActionKind::none &&
      g_tuner_drawer_open && spectrum_width() == kSpectrumOpenW;
  const bool drawer_ok =
      handle_touch(center_x(kTunerAgc), center_y(kTunerAgc)).kind ==
          ActionKind::gain_auto &&
      handle_gain_drag(kGainX + kGainW, kGainY).value == 496 &&
      handle_touch(kDrawer.x + 8, kSpectrumY + 20).kind == ActionKind::none &&
       handle_touch(kSpectrumX + kSpectrumOpenW - 8, kSpectrumY + 60).kind ==
           ActionKind::tune_hz;
  g_state.open(Modal::frequency);
  const bool modal_gestures_ok =
      !spectrum_contains(kSpectrumX + 20, kSpectrumY + 20) &&
      handle_filter_drag(filter_edge, kSpectrumY + 20, true).kind ==
          ActionKind::none &&
      handle_gain_drag(kGainX, kGainY).kind == ActionKind::none;
  g_state.close_modal();
  g_snapshot.controls.route = ReceiverRoute::direct_q;
  const bool direct_q_ok =
                           handle_touch(center_x(kTunerAgc), center_y(kTunerAgc)).kind ==
                               ActionKind::none &&
                           handle_gain_drag(kGainX, kGainY).kind == ActionKind::none &&
                           receiver_controls::item(
                               receiver_controls::Control::rf_gain,
                               g_snapshot.controls).availability !=
                               receiver_controls::Availability::enabled;
  const bool tuner_closes =
      handle_touch(center_x(kDrawerClose), center_y(kDrawerClose)).kind ==
          ActionKind::none &&
      !g_tuner_drawer_open && spectrum_width() == kSpectrumClosedW;
  const Rect controls[] = {kStep, kFilterDown, kFilterValue, kFilterUp,
                               kMode, kClean, kBoost, kNoiseReduction, kNotch,
                               kSquelch, kTuner};
  bool geometry_ok = kFrequencyCard.x >= 0 && kFrequencyCard.y >= 0 &&
      kFrequencyCard.x + kFrequencyCard.w <= 1280 &&
      kFrequencyCard.y + kFrequencyCard.h <= 720 &&
      kSpectrumX + kSpectrumClosedW <= 1280 &&
      kSpectrumY + kSpectrumH <= 720 &&
      kWaterfallY + kWaterfallH <= 720 &&
      kDrawer.x + kDrawer.w <= 1280 && kDrawer.y + kDrawer.h <= 720;
  for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
    geometry_ok = geometry_ok && controls[i].x >= 0 && controls[i].y >= 0 &&
        controls[i].x + controls[i].w <= 1280 &&
        controls[i].y + controls[i].h <= 720;
    for (size_t j = i + 1; j < sizeof(controls) / sizeof(controls[0]); ++j)
      geometry_ok = geometry_ok && !overlaps(controls[i], controls[j]);
  }
  const bool spectrum_layout_ok =
      spectrum_x_for_bin(0, 4) == kSpectrumX &&
      spectrum_x_for_bin(3, 4) == kSpectrumX + kSpectrumClosedW - 1 &&
      spectrum_tick_frequency(5850000, 480000, 0, 5) == 5610000 &&
      spectrum_tick_frequency(5850000, 480000, 2, 5) == 5850000 &&
      spectrum_tick_frequency(5850000, 480000, 4, 5) == 6090000 &&
      spectrum_tick_frequency(30000000, 2400000, 0, 5) == 28800000 &&
      spectrum_tick_frequency(30000000, 2400000, 1, 5) == 29400000 &&
      spectrum_tick_frequency(30000000, 2400000, 2, 5) == 30000000 &&
      spectrum_tick_frequency(30000000, 2400000, 3, 5) == 30600000 &&
      spectrum_tick_frequency(30000000, 2400000, 4, 5) == 31200000 &&
      handle_touch(kSpectrumX + 10, kSpectrumY + kSpectrumPlotH + 4).kind ==
          ActionKind::none &&
      kWaterfallY > kSpectrumY + kSpectrumH &&
      kWaterfallY + kWaterfallH <= kZoomY && kZoomY + 37 < kTabsY;
  const float resolution_test[] = {-80.0f, -20.0f, -75.0f, -40.0f};
  const bool peak_pool_ok =
      spectrum::peak_for_pixel(resolution_test, 0, 4, 0, 2) == -20.0f &&
      spectrum::peak_for_pixel(resolution_test, 0, 4, 1, 2) == -40.0f;
  g_snapshot.frequency_hz = 7100000;
  g_snapshot.span_hz = 480000;
  const bool touch_tune_bounds_ok =
      handle_touch(kSpectrumX + kSpectrumClosedW / 2, kSpectrumY).kind ==
          ActionKind::tune_hz &&
      handle_touch(kSpectrumX + kSpectrumClosedW / 2, kSpectrumY).value == 7100000 &&
      handle_touch(kSpectrumX - 1, kSpectrumY).kind == ActionKind::none &&
      handle_touch(kSpectrumX, kSpectrumY - 1).kind == ActionKind::none &&
      handle_touch(kSpectrumX + kSpectrumClosedW, kWaterfallY).kind == ActionKind::none &&
      handle_touch(kSpectrumX, kWaterfallY + kWaterfallH).kind == ActionKind::none &&
       handle_touch(kSpectrumX + 20, kSpectrumY + kSpectrumPlotH + 8).kind ==
           ActionKind::none;
  g_snapshot.frequency_hz = 5000000;
  const bool on_air_ok =
      handle_touch(kTabW + kTabW / 2, kTabsY + 20).kind == ActionKind::none &&
      handle_touch(640, 245).kind == ActionKind::tune_hz &&
      handle_touch(640, 245).value == 5000000;
  g_snapshot.frequency_hz = 5200000;
  const bool on_air_filter_ok = handle_touch(640, 245).kind == ActionKind::none;
  const bool hunt_cancel_ok =
      handle_touch(2 * kTabW + kTabW / 2, kTabsY + 20).kind == ActionKind::none &&
      handle_touch(3 * kTabW + kTabW / 2, kTabsY + 20).kind ==
          ActionKind::hunt_cancel;
  g_snapshot = saved;
  g_active = was_active;
  g_filter_dragging = was_filter_dragging;
  g_tuner_drawer_open = was_drawer_open;
  g_memory_page = saved_memory_page;
  g_state.open(saved_modal);
  g_state.select_tab(saved_tab);
  memcpy(g_entry, saved_entry, sizeof(g_entry));
  g_self_check_running = false;
  return controls_ok && hidden_drawer_ok && filter_drag_ok && tuner_opens &&
          drawer_ok && modal_gestures_ok && direct_q_ok && tuner_closes && geometry_ok &&
          spectrum_layout_ok && peak_pool_ok && touch_tune_bounds_ok &&
          on_air_ok && on_air_filter_ok && hunt_cancel_ok &&
          kTabsY + 90 <= 720 && model_self_check() && receiver_controls::self_check();
}

}  // namespace orcsdr::shortwave

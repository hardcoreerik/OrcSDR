#include "focus_nav.hpp"
#include "waterfall_style.hpp"
#include "home_dashboard.hpp"
#include "freq_keypad.hpp"

#include "dashboard_audio_control.hpp"
#include "orc_badge.hpp"
#include "spectrum_resample.hpp"

#include <M5Unified.h>
#include <esp_attr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace orcsdr::home {
namespace {

constexpr uint16_t kCyan = 0x05FF;
constexpr uint16_t kGreen = 0x6FE0;
constexpr uint16_t kDim = 0x4228;
constexpr uint16_t kPanel = 0x0021;
constexpr int kRailX = 24, kRailY = 112, kRailW = 280, kRailH = 530;
constexpr int kMainX = 318, kMainY = 80, kMainW = 930, kMainH = 562;
constexpr int kPlotX = 330, kPlotW = 906;
constexpr int kSpectrumY = 112, kSpectrumH = 186;
constexpr int kWaterfallY = 301, kWaterfallH = 194;   // ends 7 px above the readout strip so its focus ring (6 px out) stays clear of the scrolling waterfall
// Readout strip under the graphics: frequency on the left, waterfall contrast, then the mode chip.
constexpr int kReadoutY = 504, kReadoutH = 52;
constexpr int kContrastY = kReadoutY + (kReadoutH - 28) / 2;
constexpr int kContrastDownX = 690;
constexpr int kContrastUpX = 854;
constexpr int kModeX = 1144, kModeW = 92;
constexpr int kPaletteX = 898, kPaletteW = 120;
constexpr int kSpeedX = 1026, kSpeedW = 110;
// Control row: SPAN / TUNE / STEP SIZE steppers, then FILTER and GAIN.
constexpr int kControlY = 564, kControlH = 62;
constexpr int kStepperW = 208, kStepperSpan = 330, kStepperTune = 544, kStepperSize = 758;
constexpr int kStepperArrowW = 34, kStepperArrowH = 48, kStepperArrowInset = 4;
constexpr int kContrastButtonW = 36;
constexpr int kContrastButtonH = 28;
constexpr int kListX = 30, kListY = 158, kListW = 242, kListH = 420;
constexpr int kRowH = 52, kRowGap = 8, kRowPitch = kRowH + kRowGap;
constexpr int kVisibleRows = 7;
constexpr int kAllY = 590;
constexpr int kTapDragThreshold = 10;
constexpr int kHeaderStatusX = audio_header::kStatusBarX;
constexpr int kHeaderStatusW = audio_header::kStatusBarW;
constexpr size_t kBrowserColumns = 3;
constexpr size_t kBrowserRows = 4;
constexpr size_t kBrowserPageSize = kBrowserColumns * kBrowserRows;
constexpr int kBrowserCardX = 30, kBrowserCardY = 96;
constexpr int kBrowserCardW = 390, kBrowserCardH = 118;
constexpr int kBrowserColumnPitch = 410, kBrowserRowPitch = 136;
constexpr int kBrowserNavY = 650;
constexpr int kGainX = 1124, kGainY = 564, kGainW = 112, kGainH = 62;
// The popups dock at the bottom of the screen (over the readout strip, controls and footer) so the spectrum and most
// of the waterfall keep running above them. kPopShift moves the original popup layout down.
constexpr int kPopShift = 258;
constexpr int kPopX = 340, kPopY = 150 + kPopShift, kPopW = 886, kPopH = 300;
constexpr int kPopButtonY = 190 + kPopShift, kPopButtonH = 50;
constexpr int kAutoX = 360, kManualX = 584, kRtlAgcX = 808, kCloseX = 1032;
constexpr int kPopButtonW = 210, kCloseW = 178;
constexpr int kSliderY = 276 + kPopShift, kSliderH = 50;
constexpr int kMinusX = 360, kBarX = 446, kBarW = 678, kPlusX = 1140, kNudgeW = 70;
constexpr uint32_t kSpanSteps[] = {
    120000, 240000, 480000, 960000, 1200000, 2400000};

Snapshot current{};
bool shown = false;
bool browser = false;
bool gain_popup = false;
bool keypad = false;
bool mode_popup = false;
char keypad_entry[12]{};
bool filter_popup = false;
bool filter_edges = false;   // draw the receive-filter edges on the spectrum
size_t browser_page = 0;
int32_t scroll_offset_px = 0;
uint32_t last_spectrum_ms = 0;
EXT_RAM_BSS_ATTR float spectrum_levels[512]{};
uint8_t waterfall_contrast = 5;
// Spectrum ceiling (dB): jumps up to the strongest bin at once, falls back slowly, so a strong
// station is never drawn flat against the top edge and the scale does not pump.
float spectrum_ceiling = 0.0f;
bool spectrum_ceiling_valid = false;
constexpr float kSpectrumHeadroomDb = 5.0f;
constexpr float kSpectrumMinRangeDb = 30.0f;
// The frequency labels occupy the bottom 24 px of the plot, so the trace baseline sits above them,
// and the floor is lowered a little so the noise floor is drawn off the baseline, not hidden on it.
constexpr int kSpectrumAxisBandPx = 26;
constexpr float kSpectrumFloorMarginDb = 4.0f;

struct Gesture {
  bool down = false;
  bool scrolling = false;
  bool thumb = false;
  int32_t start_x = 0;
  int32_t start_y = 0;
  int32_t start_offset = 0;
} gesture;

bool inside(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color, uint8_t size,
          textdatum_t datum = middle_left) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextSize(size);
  M5.Display.drawString(value, x, y);
}

// A panel that draws but is not a keyboard focus stop: chrome, read-only chips and the body that
// surrounds a control's own buttons.
void frame(int x, int y, int w, int h, uint16_t color = kCyan, int radius = 10) {
  M5.Display.fillRoundRect(x, y, w, h, radius, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, radius, color);
}

// A panel that is itself a control: the keyboard can focus it and Enter taps its centre.
void panel(int x, int y, int w, int h, uint16_t color = kCyan, int radius = 10) {
  orcsdr::focus_nav::note(x, y, w, h);
  frame(x, y, w, h, color, radius);
}

int recent_content_rows() {
  return 1 + static_cast<int>(dashboards::recent_count());
}

int max_scroll_px() {
  return std::max(0, recent_content_rows() * kRowPitch - kListH);
}

void clamp_scroll() {
  scroll_offset_px = std::clamp<int32_t>(scroll_offset_px, 0, max_scroll_px());
}

void draw_wifi_icon(int x, int y, uint16_t color) {
  M5.Display.drawArc(x, y, 22, 19, 215, 325, color);
  M5.Display.drawArc(x, y, 14, 11, 215, 325, color);
  M5.Display.fillCircle(x, y + 7, 3, color);
}

void draw_usb_icon(int x, int y, uint16_t color) {
  M5.Display.drawFastVLine(x, y - 17, 31, color);
  M5.Display.drawLine(x, y - 17, x - 5, y - 10, color);
  M5.Display.drawLine(x, y - 17, x + 5, y - 10, color);
  M5.Display.drawLine(x, y + 2, x - 9, y - 6, color);
  M5.Display.fillCircle(x - 10, y - 7, 3, color);
  M5.Display.drawLine(x, y + 9, x + 9, y + 2, color);
  M5.Display.drawRect(x + 7, y - 1, 5, 5, color);
}

void draw_menu_icon(dashboards::Id id, int x, int y, uint16_t color) {
  if (id == dashboards::Id::home) {
    M5.Display.fillTriangle(x - 14, y, x, y - 13, x + 14, y, color);
    M5.Display.drawRect(x - 10, y, 20, 15, color);
  } else if (id == dashboards::Id::fm || id == dashboards::Id::am) {
    M5.Display.drawRoundRect(x - 15, y - 10, 30, 22, 4, color);
    M5.Display.drawCircle(x + 7, y + 1, 5, color);
    M5.Display.drawLine(x - 10, y - 14, x + 10, y - 20, color);
  } else if (id == dashboards::Id::p25) {
    M5.Display.drawRect(x - 9, y - 17, 18, 34, color);
    M5.Display.drawFastVLine(x - 3, y - 11, 12, color);
    M5.Display.drawFastVLine(x + 3, y - 11, 12, color);
  } else if (id == dashboards::Id::adsb) {
    M5.Display.fillTriangle(x - 18, y + 9, x + 18, y - 12, x + 3, y + 14, color);
    M5.Display.drawFastVLine(x - 2, y - 7, 27, color);
  } else if (id == dashboards::Id::shortwave || id == dashboards::Id::airband ||
             id == dashboards::Id::marine || id == dashboards::Id::satellite) {
    M5.Display.drawCircle(x, y, 17, color);
    M5.Display.drawFastHLine(x - 17, y, 34, color);
    M5.Display.drawFastVLine(x, y - 17, 34, color);
  } else if (id == dashboards::Id::lora) {
    M5.Display.fillCircle(x, y - 2, 3, color);
    M5.Display.drawFastVLine(x, y + 1, 16, color);
    M5.Display.drawArc(x, y - 2, 11, 9, 300, 60, color);
    M5.Display.drawArc(x, y - 2, 11, 9, 120, 240, color);
    M5.Display.drawArc(x, y - 2, 18, 16, 300, 60, color);
    M5.Display.drawArc(x, y - 2, 18, 16, 120, 240, color);
  } else if (id == dashboards::Id::rf_lab) {
    M5.Display.drawRoundRect(x - 18, y - 14, 36, 28, 4, color);
    M5.Display.drawLine(x - 14, y + 3, x - 8, y + 3, color);
    M5.Display.drawLine(x - 8, y + 3, x - 3, y - 8, color);
    M5.Display.drawLine(x - 3, y - 8, x + 3, y + 9, color);
    M5.Display.drawLine(x + 3, y + 9, x + 8, y - 3, color);
    M5.Display.drawLine(x + 8, y - 3, x + 14, y - 3, color);
  } else if (id == dashboards::Id::settings) {
    M5.Display.drawCircle(x, y, 15, color);
    M5.Display.fillCircle(x, y, 5, color);
  } else if (id == dashboards::Id::wifi_analysis) {
    M5.Display.drawCircle(x, y + 7, 3, color);
    M5.Display.drawArc(x, y + 7, 12, 12, 210, 330, color);
    M5.Display.drawArc(x, y + 7, 20, 20, 210, 330, color);
  } else {
    M5.Display.drawLine(x - 13, y - 13, x + 13, y + 13, color);
    M5.Display.drawLine(x + 13, y - 13, x - 13, y + 13, color);
    M5.Display.drawCircle(x, y, 5, color);
  }
}

// Top edge and height match the Home / sound / settings buttons (y 12..66).
constexpr int kHeaderStatusY = audio_header::kStatusBarY, kHeaderStatusH = audio_header::kStatusBarH;

void draw_header_status() {
  const int y = kHeaderStatusY;
  M5.Display.fillRect(kHeaderStatusX, y, kHeaderStatusW, kHeaderStatusH, TFT_BLACK);
  frame(kHeaderStatusX, y, kHeaderStatusW, kHeaderStatusH, kCyan, 9);
  draw_wifi_icon(kHeaderStatusX + 26, y + 32, current.wifi_connected ? kCyan : kDim);
  text("Wi-Fi", kHeaderStatusX + 54, y + 17, TFT_WHITE, 2);
  text(current.wifi_connected && current.wifi_ip[0] ? current.wifi_ip : "OFFLINE",
       kHeaderStatusX + 54, y + 38, current.wifi_connected ? kCyan : TFT_ORANGE, 1);
  M5.Display.drawFastVLine(kHeaderStatusX + 114, y + 8, kHeaderStatusH - 16, kDim);
  draw_usb_icon(kHeaderStatusX + 135, y + 30, current.driver_ready ? kCyan : kDim);
  text("RTL-SDR", kHeaderStatusX + 148, y + 17, TFT_WHITE, 2);
  text(current.driver_ready ? "READY" : "NOT READY", kHeaderStatusX + 158, y + 38,
       current.driver_ready ? kCyan : TFT_ORANGE, 1);
  M5.Display.drawFastVLine(kHeaderStatusX + 238, y + 8, kHeaderStatusH - 16, kDim);
  text(current.clock[0] ? current.clock : "--:--", kHeaderStatusX + kHeaderStatusW - 12, y + 17,
       TFT_WHITE, 2, middle_right);
  text(current.date[0] ? current.date : "UPTIME", kHeaderStatusX + kHeaderStatusW - 12, y + 39,
       kCyan, 2, middle_right);
}

void draw_header() {
  audio_header::draw_brand("HOME");
  draw_header_status();
  audio_header::draw_battery(current.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_visualizer_button(current.receiving);
  audio_header::draw_settings_button();
  audio_header::draw_mute_button(current.sound_enabled);
}

void draw_recent_list() {
  clamp_scroll();
  M5.Display.startWrite();
  M5.Display.setClipRect(kListX, kListY, kListW, kListH);
  M5.Display.fillRect(kListX, kListY, kListW, kListH, TFT_BLACK);
  const int first = scroll_offset_px / kRowPitch;
  const int offset = -(scroll_offset_px % kRowPitch);
  const int rows = recent_content_rows();
  for (int slot = 0; slot <= kVisibleRows; ++slot) {
    const int index = first + slot;
    if (index >= rows) break;
    const dashboards::Id id = index == 0 ? dashboards::Id::home
                                         : dashboards::recent(index - 1);
    const auto* entry = id == dashboards::Id::home ? nullptr : dashboards::find(id);
    const char* label = id == dashboards::Id::home ? "HOME"
                                                   : (entry ? entry->title : "UNKNOWN");
    const int y = kListY + offset + slot * kRowPitch;
    const bool selected = id == dashboards::Id::home;
    orcsdr::focus_nav::note(kListX + 2, y, kListW - 20, kRowH);
    M5.Display.fillRoundRect(kListX + 2, y, kListW - 20, kRowH, 7,
                             selected ? 0x00A0 : TFT_BLACK);
    M5.Display.drawRoundRect(kListX + 2, y, kListW - 20, kRowH, 7,
                             selected ? kGreen : kCyan);
    draw_menu_icon(id, kListX + 34, y + kRowH / 2, selected ? kGreen : kCyan);
    text(label, kListX + 67, y + kRowH / 2,
         selected ? kGreen : TFT_WHITE, 2);
  }
  M5.Display.clearClipRect();

  const int track_x = kListX + kListW - 10;
  M5.Display.fillRoundRect(track_x, kListY, 7, kListH, 4, 0x1082);
  if (max_scroll_px() > 0) {
    const int content_h = recent_content_rows() * kRowPitch;
    const int thumb_h = std::max(36, kListH * kListH / content_h);
    const int thumb_y = kListY + scroll_offset_px * (kListH - thumb_h) /
                                     max_scroll_px();
    M5.Display.fillRoundRect(track_x - 2, thumb_y, 11, thumb_h, 5, kCyan);
  }
  M5.Display.endWrite();
}

void draw_rail() {
  frame(kRailX, kRailY, kRailW, kRailH, kCyan, 12);
  text("LAST USED", 34, 132, kCyan, 2);
  M5.Display.drawFastHLine(132, 132, 110, kCyan);
  draw_recent_list();
  panel(kListX, kAllY, kListW - 2, 42, kCyan, 7);
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col)
      M5.Display.fillRect(kListX + 20 + col * 11, kAllY + 12 + row * 9, 6, 6,
                          kCyan);
  text("ALL DASHBOARDS", kListX + 65, kAllY + 21, kCyan, 2);
}

uint16_t waterfall_color(float value) {
  return waterfall_style::color565(waterfall_style::Screen::home, value);
}

uint8_t waterfall_range_db(uint8_t contrast) {
  return static_cast<uint8_t>(54 - std::clamp<uint8_t>(contrast, 1, 7) * 6);
}

void draw_waterfall_controls() {
  M5.Display.fillRect(kContrastDownX, kContrastY, kContrastUpX + kContrastButtonW - kContrastDownX,
                      kContrastButtonH, TFT_BLACK);
  panel(kContrastDownX, kContrastY, kContrastButtonW, kContrastButtonH, kCyan, 5);
  text("<", kContrastDownX + kContrastButtonW / 2, kContrastY + 14, kGreen, 2,
       middle_center);
  char value[24];
  snprintf(value, sizeof(value), "CONTRAST %u", waterfall_contrast);
  text(value, (kContrastDownX + kContrastUpX + kContrastButtonW) / 2, kContrastY + 14, kCyan, 2, middle_center);
  panel(kContrastUpX, kContrastY, kContrastButtonW, kContrastButtonH, kCyan, 5);
  text(">", kContrastUpX + kContrastButtonW / 2, kContrastY + 14, kGreen, 2,
       middle_center);
  M5.Display.fillRect(kPaletteX, kReadoutY, kPaletteW, kReadoutH, TFT_BLACK);
  panel(kPaletteX, kReadoutY, kPaletteW, kReadoutH, kCyan, 8);
  text("PALETTE", kPaletteX + kPaletteW / 2, kReadoutY + 15, kCyan, 2, middle_center);
  text(waterfall_style::palette_name(waterfall_style::palette(waterfall_style::Screen::home)), kPaletteX + kPaletteW / 2,
       kReadoutY + 38, kGreen, 2, middle_center);
  M5.Display.fillRect(kSpeedX, kReadoutY, kSpeedW, kReadoutH, TFT_BLACK);
  panel(kSpeedX, kReadoutY, kSpeedW, kReadoutH, kCyan, 8);
  text("SPEED", kSpeedX + kSpeedW / 2, kReadoutY + 15, kCyan, 2, middle_center);
  text(waterfall_style::speed_name(waterfall_style::speed(waterfall_style::Screen::home)), kSpeedX + kSpeedW / 2, kReadoutY + 38, kGreen,
       2, middle_center);
}

float home_spectrum_floor(const float* levels, size_t count, float pipeline_floor) {
  float sum = 0.0f;
  for (size_t i = 0; i < count; ++i) sum += levels[i];
  return std::max(pipeline_floor, sum / static_cast<float>(count) - 4.0f);
}

void draw_mode_chip(const char* mode) {
  M5.Display.fillRect(kModeX, kReadoutY, kModeW, kReadoutH, TFT_BLACK);
  frame(kModeX, kReadoutY, kModeW, kReadoutH, kCyan, 8);
  text("MODE", kModeX + kModeW / 2, kReadoutY + 15, kCyan, 2, middle_center);
  text(mode, kModeX + kModeW / 2, kReadoutY + 38, kGreen, 2, middle_center);
}

void draw_frequency() {
  M5.Display.fillRect(kPlotX, kReadoutY, 350, kReadoutH, TFT_BLACK);
  char value[40];
  snprintf(value, sizeof(value), current.frequency_hz >= 1000000 ? "%.3f" : "%.1f",
           current.frequency_hz >= 1000000 ? current.frequency_hz / 1000000.0
                                           : current.frequency_hz / 1000.0);
  const int mid = kReadoutY + kReadoutH / 2;
  text(value, kPlotX + 8, mid, kGreen, 4);
  text(current.frequency_hz >= 1000000 ? "MHz" : "kHz", kPlotX + 215, mid + 8,
       kGreen, 2);
  if (current.requested_frequency_hz != 0 &&
      current.requested_frequency_hz != current.frequency_hz)
    text("TUNING", kPlotX + 330, mid + 8, TFT_YELLOW, 1, middle_right);
  draw_waterfall_controls();
}

void format_spectrum_frequency(char* output, size_t output_size, uint32_t frequency_hz) {
  if (frequency_hz >= 1000000u)
    snprintf(output, output_size, "%.3f MHz", frequency_hz / 1000000.0);
  else
    snprintf(output, output_size, "%.1f kHz", frequency_hz / 1000.0);
}

void draw_spectrum_axis() {
  const uint32_t half_span = current.span_hz / 2u;
  const uint32_t low = current.frequency_hz > half_span ? current.frequency_hz - half_span : 0u;
  const uint32_t high = current.frequency_hz + half_span;
  char low_text[20], center_text[20], high_text[20];
  format_spectrum_frequency(low_text, sizeof(low_text), low);
  format_spectrum_frequency(center_text, sizeof(center_text), current.frequency_hz);
  format_spectrum_frequency(high_text, sizeof(high_text), high);
  M5.Display.fillRect(kPlotX + 1, kSpectrumY + kSpectrumH - 23, kPlotW - 2, 22, TFT_BLACK);
  text(low_text, kPlotX + 5, kSpectrumY + kSpectrumH - 11, TFT_LIGHTGREY, 2);
  text(center_text, kPlotX + kPlotW / 2, kSpectrumY + kSpectrumH - 11, TFT_LIGHTGREY, 2,
       middle_center);
  text(high_text, kPlotX + kPlotW - 5, kSpectrumY + kSpectrumH - 11, TFT_LIGHTGREY, 2,
       middle_right);
}

// One "< LABEL value >" group in the control row; the arrows are the touch targets.
void draw_stepper(int x, const char* label, const char* value) {
  frame(x, kControlY, kStepperW, kControlH, kCyan, 9);
  panel(x + kStepperArrowInset, kControlY + 7, kStepperArrowW, kStepperArrowH, kDim, 5);
  text("<", x + kStepperArrowInset + kStepperArrowW / 2, kControlY + 31, kGreen, 3, middle_center);
  panel(x + kStepperW - kStepperArrowInset - kStepperArrowW, kControlY + 7, kStepperArrowW,
        kStepperArrowH, kDim, 5);
  text(">", x + kStepperW - kStepperArrowInset - kStepperArrowW / 2, kControlY + 31, kGreen, 3,
       middle_center);
  text(label, x + kStepperW / 2, kControlY + 15, kCyan, 2, middle_center);
  text(value, x + kStepperW / 2, kControlY + 43, kGreen, 2, middle_center);
}

void draw_step_size_controls();

void draw_span_control() {
  char value[24];
  snprintf(value, sizeof(value), "%.0f kHz", current.span_hz / 1000.0);
  draw_stepper(kStepperSpan, "SPAN", value);
}

void draw_tuning_controls() {
  M5.Display.fillRect(kStepperSpan, kControlY, kStepperSize + kStepperW - kStepperSpan, kControlH,
                      TFT_BLACK);
  char value[24];
  draw_span_control();
  snprintf(value, sizeof(value), "%.1f kHz", current.step_hz / 1000.0);
  draw_stepper(kStepperTune, "TUNE", value);
  draw_step_size_controls();
}

void draw_step_size_controls() {
  char value[24];
  snprintf(value, sizeof(value), "%.1f kHz", current.step_hz / 1000.0);
  draw_stepper(kStepperSize, "STEP SIZE", current.step_adjustable ? value : "FIXED");
}

void footer_text(const char* value, int x, uint16_t color) {
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color, kPanel);
  M5.Display.setTextSize(2);
  M5.Display.drawString(value, x, 678);
}

bool gain_changed(const Snapshot& a, const Snapshot& b) {
  return a.driver_ready != b.driver_ready || a.gain_available != b.gain_available ||
         a.gain_auto_available != b.gain_auto_available || a.gain_smart != b.gain_smart ||
         a.gain_auto != b.gain_auto || a.gain_tenth_db != b.gain_tenth_db ||
         a.rtl_agc_available != b.rtl_agc_available || a.rtl_agc != b.rtl_agc ||
         a.bias_available != b.bias_available || a.bias_on != b.bias_on ||
         a.gain_step_count != b.gain_step_count;
}

// "SMART" / "AGC" / "33.8" / "N/A"; the color says auto (green) or manual (orange).
uint16_t gain_label(char* out, size_t size) {
  if (!current.driver_ready || !current.gain_available) {
    snprintf(out, size, "N/A");
    return TFT_LIGHTGREY;
  }
  if (current.gain_auto) {
    snprintf(out, size, current.gain_smart ? "SMART" : "AGC");
    return kGreen;
  }
  snprintf(out, size, "%.1f", current.gain_tenth_db / 10.0);
  return TFT_ORANGE;
}

void draw_gain_panel() {
  panel(kGainX, kGainY, kGainW, kGainH, kCyan, 7);
  text("GAIN", kGainX + kGainW / 2, 582, kCyan, 2, middle_center);
  char label[16];
  const uint16_t color = gain_label(label, sizeof(label));
  char value[24];
  if (color == TFT_ORANGE) snprintf(value, sizeof(value), "%s dB", label);
  else if (color == kGreen && current.gain_smart)
    snprintf(value, sizeof(value), "%s %.0f", label, current.gain_tenth_db / 10.0);
  else strlcpy(value, label, sizeof(value));
  text(value, kGainX + kGainW / 2, 608, color, 2, middle_center);
}

void draw_gain_chip() {
  frame(1064, 86, 108, 26, kCyan, 6);
  char label[16];
  const uint16_t color = gain_label(label, sizeof(label));
  char value[24];
  snprintf(value, sizeof(value), "GAIN %s%s", label, current.rtl_agc ? " +R" : "");
  text(value, 1118, 99, color, 1, middle_center);
}

void draw_footer_gain() {
  M5.Display.fillRect(564, 660, 132, 36, kPanel);
  char label[16];
  const uint16_t color = gain_label(label, sizeof(label));
  char value[24];
  snprintf(value, sizeof(value), "GAIN %s", label);
  footer_text(value, 630, color);
}

void draw_footer_bias() {
  M5.Display.fillRect(704, 660, 148, 36, kPanel);
  if (!current.driver_ready || !current.bias_available) {
    footer_text("BIAS N/A", 778, TFT_LIGHTGREY);
    return;
  }
  footer_text(current.bias_on ? "BIAS ON" : "BIAS OFF", 778,
              current.bias_on ? TFT_RED : TFT_LIGHTGREY);
}

void popup_button(int x, int w, const char* label, bool on, bool enabled,
                  int y = kPopButtonY) {
  if (enabled) focus_nav::note(x, y, w, kPopButtonH);
  const uint16_t fill = on && enabled ? 0x0320 : kPanel;
  M5.Display.fillRoundRect(x, y, w, kPopButtonH, 8, fill);
  M5.Display.drawRoundRect(x, y, w, kPopButtonH, 8,
                           !enabled ? kDim : on ? kGreen : kCyan);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(!enabled ? kDim : on ? TFT_WHITE : TFT_LIGHTGREY, fill);
  M5.Display.setTextSize(2);
  M5.Display.drawString(label, x + w / 2, y + kPopButtonH / 2);
}

int gain_step_index() {
  int best = 0;
  for (int i = 1; i < current.gain_step_count; ++i)
    if (std::abs(current.gain_steps_tenth_db[i] - current.gain_tenth_db) <
        std::abs(current.gain_steps_tenth_db[best] - current.gain_tenth_db))
      best = i;
  return best;
}

void draw_gain_popup() {
  M5.Display.fillRoundRect(kPopX, kPopY, kPopW, kPopH, 12, TFT_BLACK);
  M5.Display.drawRoundRect(kPopX, kPopY, kPopW, kPopH, 12, kCyan);
  M5.Display.drawRoundRect(kPopX + 1, kPopY + 1, kPopW - 2, kPopH - 2, 11, kCyan);
  text("RECEIVER GAIN", kPopX + 20, kPopY + 20, kCyan, 2);
  text(current.receiver, kPopX + kPopW - 20, kPopY + 20,
       current.driver_ready ? kGreen : TFT_ORANGE, 2, middle_right);
  const bool gain = current.driver_ready && current.gain_available;
  popup_button(kAutoX, kPopButtonW, current.gain_smart ? "SMART" : "TUNER AGC",
               current.gain_auto, gain && current.gain_auto_available);
  popup_button(kManualX, kPopButtonW, "MANUAL", !current.gain_auto, gain);
  popup_button(kRtlAgcX, kPopButtonW, current.rtl_agc ? "RTL AGC ON" : "RTL AGC OFF",
               current.rtl_agc, current.driver_ready && current.rtl_agc_available);
  popup_button(kCloseX, kCloseW, "CLOSE", false, true);
  if (!gain || current.gain_step_count < 2) {
    text(current.driver_ready ? "Tuner gain is not available at this frequency."
                              : "No receiver connected.",
         kMinusX, kSliderY + kSliderH / 2, TFT_LIGHTGREY, 2);
  } else {
    char value[48];
    snprintf(value, sizeof(value), "%s %.1f dB",
             current.gain_auto ? "AUTO SELECTED" : "MANUAL GAIN",
             current.gain_tenth_db / 10.0);
    text(value, kMinusX, 258 + kPopShift, current.gain_auto ? kGreen : TFT_ORANGE, 2);
    panel(kMinusX, kSliderY, kNudgeW, kSliderH, kCyan, 8);
    text("-", kMinusX + kNudgeW / 2, kSliderY + kSliderH / 2, TFT_WHITE, 3, middle_center);
    panel(kPlusX, kSliderY, kNudgeW, kSliderH, kCyan, 8);
    text("+", kPlusX + kNudgeW / 2, kSliderY + kSliderH / 2, TFT_WHITE, 3, middle_center);
    M5.Display.drawRect(kBarX, kSliderY, kBarW, kSliderH, kDim);
    const int count = current.gain_step_count;
    const int selected = gain_step_index();
    for (int i = 0; i < count; ++i) {
      const int x0 = kBarX + 2 + i * (kBarW - 4) / count;
      const int x1 = kBarX + 2 + (i + 1) * (kBarW - 4) / count;
      const uint16_t color = i > selected ? kDim : current.gain_auto ? kGreen : TFT_ORANGE;
      M5.Display.fillRect(x0, kSliderY + 4, std::max(1, x1 - x0 - 2), kSliderH - 8, color);
    }
  }
  text(current.gain_smart ? "SMART: OrcSDR picks the lowest gain that sounds clean."
                          : "TUNER AGC: the tuner chip sets its own gain.",
       kPopX + 20, 356 + kPopShift, TFT_LIGHTGREY, 2);
  text("MANUAL: you choose the tuner (RF) gain step; tap the bar.",
       kPopX + 20, 384 + kPopShift, TFT_LIGHTGREY, 2);
  text("RTL AGC: extra digital gain after the ADC. Normally OFF.",
       kPopX + 20, 412 + kPopShift, TFT_LIGHTGREY, 2);
}

Action manual_gain_at(int index) {
  return {ActionKind::gain_tenth_db, dashboards::Id::count,
          static_cast<uint32_t>(std::max<int>(0, current.gain_steps_tenth_db[index]))};
}

// ---- Receiver filter popup -----------------------------------------------------------------
constexpr int kFilterPanelX = 976, kFilterPanelY = 564, kFilterPanelW = 138, kFilterPanelH = 62;
constexpr int kFilterRow2Y = 262 + kPopShift;
constexpr int kStandardX = 360, kStandardW = 250, kEdgesX = 622, kEdgesW = 250;

void format_bandwidth(char* out, size_t size, uint32_t hz) {
  if (hz == 0) snprintf(out, size, "AUTO");
  else if (hz % 1000u == 0) snprintf(out, size, "%lu kHz", static_cast<unsigned long>(hz / 1000u));
  else snprintf(out, size, "%.1f kHz", hz / 1000.0);
}

void draw_filter_panel() {
  panel(kFilterPanelX, kFilterPanelY, kFilterPanelW, kFilterPanelH, kCyan, 7);
  text("FILTER", kFilterPanelX + kFilterPanelW / 2, 582, kCyan, 2, middle_center);
  char value[24];
  format_bandwidth(value, sizeof(value), current.filter_bandwidth_hz);
  text(value, kFilterPanelX + kFilterPanelW / 2, 608, kGreen, 2, middle_center);
}

// Preset button i of n across the popup's button row.
void filter_preset_rect(int i, int n, int* x, int* w) {
  const int span = kPopX + kPopW - 20 - kAutoX;
  const int width = (span - (n - 1) * 10) / n;
  *w = width;
  *x = kAutoX + i * (width + 10);
}

// Direct tuning in MHz across the dongle's whole range; the Tab5 validates again before it tunes.
void draw_keypad() {
  freq_keypad::draw(kSpectrumY, TFT_BLACK, "ENTER FREQUENCY", "24.0 - 1766.0", "MHz", keypad_entry);
}

// ---- Mode popup: pick the demodulation for the band on screen, with a plain-language note on each mode ------
struct ModeHelp { const char* title; const char* line[3]; };
constexpr ModeHelp kModeHelp[6] = {
    {"AUTO", {"Uses the usual mode for the band you are tuned to.",
              "It changes only when you cross into another band, never inside one.",
              "Pick a mode yourself and OrcSDR remembers it for this band."}},
    {"NFM: NARROW FM", {"Most two-way voice: police, fire, ham repeaters, marine, weather.",
                        "Clear when the signal is centered, hissy when you are off it.",
                        "If voices are crackly or very quiet, try AM (aircraft use it)."}},
    {"AM", {"Aircraft voice (118-137 MHz), AM radio, CB and some utility stations.",
            "The sound rises and falls with the strength of the signal.",
            "Aircraft channels sit 25 kHz apart, so use the 25 kHz step."}},
    {"WFM: WIDE FM", {"FM broadcast stations (88-108 MHz): music, talk and news.",
                      "It needs a wide filter, so it sounds distorted on voice radios.",
                      "US stations sit on odd tenths: 88.1, 88.3 ... 107.9."}},
    {"USB: UPPER SIDEBAND", {"Ham voice on 10 m and 6 m, and some utility stations.",
                             "Sounds like Donald Duck until you tune within about 100 Hz.",
                             "Use a small step (100 Hz to 1 kHz) and tune slowly."}},
    {"LSB: LOWER SIDEBAND", {"Ham voice by convention below 10 MHz; rare in this range.",
                             "Like USB, it sounds garbled until tuned within about 100 Hz.",
                             "If USB sounds wrong on a ham signal, try LSB."}},
};
constexpr const char* kModeLabel[6] = {"AUTO", "NFM", "AM", "WFM", "USB", "LSB"};

void draw_mode_popup() {
  M5.Display.fillRoundRect(kPopX, kPopY, kPopW, kPopH, 12, TFT_BLACK);
  M5.Display.drawRoundRect(kPopX, kPopY, kPopW, kPopH, 12, kCyan);
  M5.Display.drawRoundRect(kPopX + 1, kPopY + 1, kPopW - 2, kPopH - 2, 11, kCyan);
  text("RECEIVER MODE", kPopX + 20, kPopY + 20, kCyan, 2);
  text(current.band[0] ? current.band : "--", kPopX + kPopW - 20, kPopY + 20, kGreen, 2, middle_right);
  for (int i = 0; i < 6; ++i) {
    int x = 0, w = 0;
    filter_preset_rect(i, 6, &x, &w);
    popup_button(x, w, kModeLabel[i], current.mode_choice == i, current.mode_selectable);
    if (i != 0 && current.mode_suggested == i && current.mode_selectable)
      text("USUAL", x + w - 6, kPopButtonY + 8, kGreen, 1, middle_right);
  }
  popup_button(kCloseX, kCloseW, "CLOSE", false, true, kFilterRow2Y);
  if (!current.mode_selectable) {
    text("This band sets its own mode; its dashboard chooses it for you.", kPopX + 20, 288 + kPopShift,
         TFT_LIGHTGREY, 2);
    return;
  }
  text(current.mode_choice == 0 ? "AUTO picks the usual mode here. Tap a mode to pin it."
                                : "Pinned for this band. Tap AUTO to let OrcSDR choose.",
       kPopX + 20, 288 + kPopShift, TFT_LIGHTGREY, 2);
  const int shown_mode = std::clamp<int>(current.mode_active ? current.mode_active : current.mode_choice, 1, 5);
  char title[40];
  snprintf(title, sizeof(title), current.mode_choice == 0 ? "%s (AUTO)" : "%s", kModeHelp[shown_mode].title);
  text(title, kPopX + 20, 344 + kPopShift, TFT_WHITE, 2);
  for (int i = 0; i < 3; ++i)
    text(kModeHelp[shown_mode].line[i], kPopX + 20, 372 + kPopShift + i * 28, TFT_LIGHTGREY, 2);
}

Action mode_popup_action(int32_t x, int32_t y) {
  if (inside(x, y, kCloseX, kFilterRow2Y, kCloseW, kPopButtonH) ||
      !inside(x, y, kPopX, kPopY, kPopW, kPopH))
    return {ActionKind::mode_close};
  if (!current.mode_selectable) return {};
  for (int i = 0; i < 6; ++i) {
    int px = 0, pw = 0;
    filter_preset_rect(i, 6, &px, &pw);
    if (inside(x, y, px, kPopButtonY, pw, kPopButtonH))
      return {ActionKind::mode_set, dashboards::Id::count, static_cast<uint32_t>(i)};
  }
  return {};
}

void draw_filter_popup() {
  const auto& standards = filter_standards::standards(current.filter_kind);
  M5.Display.fillRoundRect(kPopX, kPopY, kPopW, kPopH, 12, TFT_BLACK);
  M5.Display.drawRoundRect(kPopX, kPopY, kPopW, kPopH, 12, kCyan);
  M5.Display.drawRoundRect(kPopX + 1, kPopY + 1, kPopW - 2, kPopH - 2, 11, kCyan);
  text("RECEIVER FILTER", kPopX + 20, kPopY + 20, kCyan, 2);
  text(current.mode[0] ? current.mode : "--", kPopX + kPopW - 20, kPopY + 20, kGreen, 2,
       middle_right);
  if (standards.count == 0) {
    text("This mode sets its own filter width.", kPopX + 20, kPopY + 60, TFT_LIGHTGREY, 2);
  }
  for (int i = 0; i < standards.count; ++i) {
    int x = 0, w = 0;
    filter_preset_rect(i, standards.count, &x, &w);
    char label[16];
    format_bandwidth(label, sizeof(label), standards.presets_hz[i]);
    popup_button(x, w, label, standards.presets_hz[i] == current.filter_bandwidth_hz, true);
    if (standards.presets_hz[i] == standards.standard_hz)
      text("STD", x + w - 6, kPopButtonY + 8, kGreen, 1, middle_right);
  }
  popup_button(kStandardX, kStandardW, "STANDARD", false, standards.count != 0, kFilterRow2Y);
  popup_button(kEdgesX, kEdgesW, filter_edges ? "EDGES ON" : "EDGES OFF", filter_edges,
               current.filter_bandwidth_hz != 0, kFilterRow2Y);
  popup_button(kCloseX, kCloseW, "CLOSE", false, true, kFilterRow2Y);
  char line[64], width[16];
  format_bandwidth(width, sizeof(width), current.filter_bandwidth_hz);
  snprintf(line, sizeof(line), "BAND: %s", standards.name);
  text(line, kPopX + 20, 344 + kPopShift, TFT_WHITE, 2);
  snprintf(line, sizeof(line), "CURRENT WIDTH: %s", width);
  text(line, kPopX + 20, 372 + kPopShift, TFT_LIGHTGREY, 2);
  text("STD marks the standard width for this band. STANDARD applies it.", kPopX + 20, 400 + kPopShift,
       TFT_LIGHTGREY, 2);
  text("EDGES draws the receive filter as two lines on the spectrum.", kPopX + 20, 428 + kPopShift,
       TFT_LIGHTGREY, 2);
}

Action filter_popup_action(int32_t x, int32_t y) {
  const auto& standards = filter_standards::standards(current.filter_kind);
  if (inside(x, y, kCloseX, kFilterRow2Y, kCloseW, kPopButtonH) ||
      !inside(x, y, kPopX, kPopY, kPopW, kPopH))
    return {ActionKind::filter_close};
  if (inside(x, y, kStandardX, kFilterRow2Y, kStandardW, kPopButtonH))
    return standards.count ? Action{ActionKind::filter_standard} : Action{};
  if (inside(x, y, kEdgesX, kFilterRow2Y, kEdgesW, kPopButtonH))
    return current.filter_bandwidth_hz ? Action{ActionKind::filter_edges} : Action{};
  for (int i = 0; i < standards.count; ++i) {
    int px = 0, pw = 0;
    filter_preset_rect(i, standards.count, &px, &pw);
    if (inside(x, y, px, kPopButtonY, pw, kPopButtonH))
      return {ActionKind::filter_set, dashboards::Id::count, standards.presets_hz[i]};
  }
  return {};
}

Action gain_popup_action(int32_t x, int32_t y) {
  const bool gain = current.driver_ready && current.gain_available;
  if (inside(x, y, kCloseX, kPopButtonY, kCloseW, kPopButtonH) ||
      !inside(x, y, kPopX, kPopY, kPopW, kPopH))
    return {ActionKind::gain_close};
  if (inside(x, y, kAutoX, kPopButtonY, kPopButtonW, kPopButtonH))
    return gain && current.gain_auto_available && !current.gain_auto
               ? Action{ActionKind::gain_auto} : Action{};
  if (inside(x, y, kManualX, kPopButtonY, kPopButtonW, kPopButtonH))
    return gain && current.gain_auto && current.gain_step_count
               ? manual_gain_at(gain_step_index()) : Action{};
  if (inside(x, y, kRtlAgcX, kPopButtonY, kPopButtonW, kPopButtonH))
    return current.driver_ready && current.rtl_agc_available
               ? Action{ActionKind::rtl_agc, dashboards::Id::count, current.rtl_agc ? 0u : 1u}
               : Action{};
  if (!gain || current.gain_step_count < 2) return {};
  const int last = current.gain_step_count - 1;
  if (inside(x, y, kMinusX, kSliderY, kNudgeW, kSliderH))
    return manual_gain_at(std::max(0, gain_step_index() - 1));
  if (inside(x, y, kPlusX, kSliderY, kNudgeW, kSliderH))
    return manual_gain_at(std::min(last, gain_step_index() + 1));
  if (inside(x, y, kBarX, kSliderY, kBarW, kSliderH))
    return manual_gain_at(std::clamp(static_cast<int>((x - kBarX) * current.gain_step_count / kBarW), 0, last));
  return {};
}

void draw_footer_receiver() {
  M5.Display.fillRect(30, 660, 136, 36, kPanel);
  footer_text(current.receiver, 98, current.driver_ready ? kGreen : TFT_ORANGE);
}

void draw_footer_sample() {
  M5.Display.fillRect(176, 660, 204, 36, kPanel);
  char value[24];
  snprintf(value, sizeof(value), current.effective_sps ? "%.3f MSPS" : "-- MSPS",
           current.effective_sps / 1000000.0);
  footer_text(value, 278, current.effective_sps ? kGreen : TFT_LIGHTGREY);
}

void draw_footer_bandwidth() {
  M5.Display.fillRect(390, 660, 166, 36, kPanel);
  char value[24];
  snprintf(value, sizeof(value), "BW %lu kHz",
           static_cast<unsigned long>(current.filter_bandwidth_hz / 1000u));
  footer_text(value, 473, kGreen);
}

void draw_footer_level() {
  M5.Display.fillRect(862, 660, 212, 36, kPanel);
  char value[24];
  snprintf(value, sizeof(value), "%.0f dBFS", static_cast<double>(current.relative_dbfs));
  footer_text(value, 968, kGreen);
  const float strength = std::clamp((current.relative_dbfs + 80.0f) / 60.0f, 0.0f, 1.0f);
  for (int i = 0; i < 9; ++i)
    M5.Display.fillRect(1080 + i * 16, 669, 12, 18,
                        i < static_cast<int>(strength * 9) ? kGreen : kDim);
}

void draw_footer() {
  frame(24, 654, 1224, 48, kCyan, 8);
  for (const int x : {170, 384, 560, 700, 856})
    M5.Display.drawFastVLine(x, 662, 32, kDim);
  draw_footer_gain();
  draw_footer_bias();
  draw_footer_receiver();
  draw_footer_sample();
  draw_footer_bandwidth();
  draw_footer_level();
}

// keep_graphics: repaint after a popup closes without wiping the live spectrum and waterfall. Only the part the popup
// covered is cleared; the spectrum redraws on its next frame and the waterfall refills as it scrolls.
// The band in plain words (FM RADIO, 70 CM HAM, AIR BAND...), centered above the spectrum.
void draw_band_label() {
  M5.Display.fillRect(kPlotX + 150, 84, kPlotW - 300, 28, TFT_BLACK);
  text(current.band[0] ? current.band : "", kPlotX + kPlotW / 2, 98, kGreen, 2, middle_center);
}

void draw_footer();

void draw_receiver_chrome(bool keep_graphics = false) {
  if (keep_graphics) M5.Display.fillRect(kPopX, kPopY, kPopW, kPopH, TFT_BLACK);
  frame(kMainX, kMainY, kMainW, kMainH, kCyan, 12);
  text("SPECTRUM", kPlotX, 98, kCyan, 2);
  draw_band_label();
  text(current.receiving ? "LIVE" : "READY", 1016, 98,
       current.receiving ? kGreen : TFT_ORANGE, 1);
  if (keep_graphics) {
    M5.Display.drawRect(kPlotX, kSpectrumY, kPlotW, kSpectrumH, kDim);
    M5.Display.drawRect(kPlotX, kWaterfallY, kPlotW, kWaterfallH, kDim);
  } else {
    M5.Display.fillRect(kPlotX, kSpectrumY, kPlotW, kSpectrumH, TFT_BLACK);
    M5.Display.drawRect(kPlotX, kSpectrumY, kPlotW, kSpectrumH, kDim);
    for (int i = 1; i < 5; ++i) {
      M5.Display.drawFastHLine(kPlotX, kSpectrumY + i * kSpectrumH / 5, kPlotW, kDim);
      M5.Display.drawFastVLine(kPlotX + i * kPlotW / 5, kSpectrumY, kSpectrumH, kDim);
    }
    M5.Display.fillRect(kPlotX, kWaterfallY, kPlotW, kWaterfallH, TFT_BLACK);
    M5.Display.drawRect(kPlotX, kWaterfallY, kPlotW, kWaterfallH, kDim);
  }
  draw_gain_chip();
  draw_frequency();
  draw_mode_chip(current.mode[0] ? current.mode : "--");
  draw_tuning_controls();
  draw_filter_panel();
  // Signal level lives in the footer meter; this corner is the gain control.
  draw_gain_panel();
  if (gain_popup) draw_gain_popup();
  if (filter_popup) draw_filter_popup();
  if (mode_popup) draw_mode_popup();
  if (keep_graphics) draw_footer();   // the popup covered the footer
}

void draw_browser() {
  M5.Display.fillScreen(TFT_BLACK);
  audio_header::draw_brand("ALL DASHBOARDS");
  audio_header::draw_battery(current.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_settings_button();
  audio_header::draw_mute_button(current.sound_enabled);
  audio_header::draw_visualizer_button(current.receiving);
  const size_t pages = std::max<size_t>(1, (dashboards::count() + kBrowserPageSize - 1) /
                                              kBrowserPageSize);
  browser_page = std::min(browser_page, pages - 1);
  const size_t first = browser_page * kBrowserPageSize;
  const size_t last = std::min(first + kBrowserPageSize, dashboards::count());
  for (size_t i = first; i < last; ++i) {
    const auto* entry = dashboards::descriptor(i);
    if (!entry) continue;
    const size_t slot = i - first;
    const int col = static_cast<int>(slot % kBrowserColumns);
    const int row = static_cast<int>(slot / kBrowserColumns);
    const int x = kBrowserCardX + col * kBrowserColumnPitch;
    const int y = kBrowserCardY + row * kBrowserRowPitch;
    panel(x, y, kBrowserCardW, kBrowserCardH, entry->available ? kCyan : kDim, 10);
    draw_menu_icon(entry->id, x + 42, y + 46, entry->available ? kCyan : kDim);
    text(entry->title, x + 78, y + 38, entry->available ? TFT_WHITE : kDim, 2);
    text(entry->subtitle, x + 78, y + 70, entry->available ? TFT_LIGHTGREY : kDim, 1);
  }
  if (pages > 1) {
    panel(486, kBrowserNavY, 110, 44, browser_page ? kCyan : kDim, 8);
    text("PREV", 541, kBrowserNavY + 22, browser_page ? TFT_WHITE : kDim, 2,
         middle_center);
    char page[24];
    snprintf(page, sizeof(page), "PAGE %u / %u", static_cast<unsigned>(browser_page + 1),
             static_cast<unsigned>(pages));
    text(page, 640, kBrowserNavY + 22, kCyan, 2, middle_center);
    panel(684, kBrowserNavY, 110, 44, browser_page + 1 < pages ? kCyan : kDim, 8);
    text("NEXT", 739, kBrowserNavY + 22,
         browser_page + 1 < pages ? TFT_WHITE : kDim, 2, middle_center);
  }
  M5.Display.drawRoundRect(10, 10, 1260, 700, 14, kCyan);
}

void draw_all() {
  // Dashboards may select custom M5GFX fonts; Home owns the built-in font.
  M5.Display.setFont(nullptr);
  M5.Display.fillScreen(TFT_BLACK);
  draw_header();
  draw_rail();
  draw_receiver_chrome();
  draw_footer();
  M5.Display.drawRoundRect(10, 10, 1260, 700, 14, kCyan);
}

Action tap_action(int32_t x, int32_t y) {
  if (audio_header::settings_hit(x, y))
    return {ActionKind::open_device_settings};
  if (audio_header::mute_hit(x, y)) return {ActionKind::sound_toggle};
  if (browser) {
    if (audio_header::home_hit(x, y)) return {ActionKind::close_browser};
    const size_t pages = std::max<size_t>(1, (dashboards::count() + kBrowserPageSize - 1) /
                                                kBrowserPageSize);
    if (inside(x, y, 486, kBrowserNavY, 110, 44) && browser_page > 0)
      return {ActionKind::browser_previous};
    if (inside(x, y, 684, kBrowserNavY, 110, 44) && browser_page + 1 < pages)
      return {ActionKind::browser_next};
    const size_t first = browser_page * kBrowserPageSize;
    const size_t last = std::min(first + kBrowserPageSize, dashboards::count());
    for (size_t i = first; i < last; ++i) {
      const size_t slot = i - first;
      const int col = static_cast<int>(slot % kBrowserColumns);
      const int row = static_cast<int>(slot / kBrowserColumns);
      if (inside(x, y, kBrowserCardX + col * kBrowserColumnPitch,
                 kBrowserCardY + row * kBrowserRowPitch,
                 kBrowserCardW, kBrowserCardH)) {
        const auto* entry = dashboards::descriptor(i);
        if (entry && entry->available)
          return {ActionKind::open_dashboard, entry->id};
      }
    }
    return {};
  }
  if (mode_popup) return mode_popup_action(x, y);
  if (gain_popup) return gain_popup_action(x, y);
  if (filter_popup) return filter_popup_action(x, y);
  if (inside(x, y, kGainX, kGainY, kGainW, kGainH)) return {ActionKind::gain_open};
  if (inside(x, y, kFilterPanelX, kFilterPanelY, kFilterPanelW, kFilterPanelH))
    return {ActionKind::filter_open};
  if (inside(x, y, kContrastDownX, kContrastY, kContrastButtonW, kContrastButtonH))
    return {ActionKind::waterfall_contrast_down};
  if (inside(x, y, kContrastUpX, kContrastY, kContrastButtonW, kContrastButtonH))
    return {ActionKind::waterfall_contrast_up};
  if (inside(x, y, kModeX, kReadoutY, kModeW, kReadoutH)) return {ActionKind::mode_open};
  if (inside(x, y, kPlotX, kReadoutY, 350, kReadoutH)) return {ActionKind::keypad_open};
  if (inside(x, y, kPaletteX, kReadoutY, kPaletteW, kReadoutH))
    return {ActionKind::waterfall_palette_next};
  if (inside(x, y, kSpeedX, kReadoutY, kSpeedW, kReadoutH))
    return {ActionKind::waterfall_speed_next};
  if (inside(x, y, kListX, kAllY, kListW, 42)) return {ActionKind::open_browser};
  if (inside(x, y, kListX, kListY, kListW, kListH)) {
    const int index = (y - kListY + scroll_offset_px) / kRowPitch;
    if (index > 0 && index <= static_cast<int>(dashboards::recent_count()))
      return {ActionKind::open_dashboard, dashboards::recent(index - 1)};
    return {};
  }
  if (inside(x, y, kPlotX, kSpectrumY, kPlotW,
             kWaterfallY + kWaterfallH - kSpectrumY)) {
    const int64_t offset = (static_cast<int64_t>(x - kPlotX) * current.span_hz) /
                               kPlotW -
                           static_cast<int64_t>(current.span_hz / 2);
    const int64_t requested = static_cast<int64_t>(current.frequency_hz) + offset;
    return {ActionKind::tune_frequency, dashboards::Id::count,
            requested > 0 ? static_cast<uint32_t>(requested) : 0};
  }
  const struct { int x; ActionKind down, up; } steppers[] = {
      {kStepperSpan, ActionKind::span_down, ActionKind::span_up},
      {kStepperTune, ActionKind::step_down, ActionKind::step_up},
      {kStepperSize, ActionKind::step_size_down, ActionKind::step_size_up}};
  for (const auto& group : steppers) {
    if (inside(x, y, group.x + kStepperArrowInset, kControlY + 7, kStepperArrowW, kStepperArrowH))
      return {group.down};
    if (inside(x, y, group.x + kStepperW - kStepperArrowInset - kStepperArrowW, kControlY + 7, kStepperArrowW,
               kStepperArrowH))
      return {group.up};
  }
  return {};
}

}  // namespace

uint32_t step_span(uint32_t span_hz, int direction) {
  if (direction < 0) {
    for (auto it = std::rbegin(kSpanSteps); it != std::rend(kSpanSteps); ++it)
      if (*it < span_hz) return *it;
    return kSpanSteps[0];
  }
  for (uint32_t step : kSpanSteps)
    if (step > span_hz) return step;
  return kSpanSteps[std::size(kSpanSteps) - 1];
}

void enter(const Snapshot& snapshot) {
  current = snapshot;
  shown = true;
  spectrum_ceiling_valid = false;
  browser = false;
  gain_popup = false;
  filter_popup = false;
  mode_popup = false;
  keypad = false;
  gesture = {};
  last_spectrum_ms = 0;
  clamp_scroll();
  draw_all();
}

void leave() { shown = browser = gain_popup = filter_popup = mode_popup = keypad = false; gesture = {}; }

void draw() {
  if (!shown) return;
  if (browser) draw_browser();
  else if (keypad) draw_keypad();
  else draw_all();
}

void update(const Snapshot& snapshot) {
  if (!shown || browser || snapshot.revision == current.revision) return;
  if (keypad) {   // keep the state current underneath; nothing repaints until the keypad closes
    current = snapshot;
    return;
  }
  const bool tuner_changed = snapshot.tuner_revision != current.tuner_revision;
  const bool audio_changed = snapshot.audio_revision != current.audio_revision;
  const bool status_changed = snapshot.status_revision != current.status_revision;
  const bool receiver_changed = snapshot.driver_ready != current.driver_ready ||
                                strcmp(snapshot.receiver, current.receiver) != 0;
  const bool sample_changed = snapshot.effective_sps / 1000u != current.effective_sps / 1000u;
  const bool bandwidth_changed =
      snapshot.filter_bandwidth_hz != current.filter_bandwidth_hz;
  const bool tuning_controls_changed = snapshot.span_hz != current.span_hz ||
                                       snapshot.step_hz != current.step_hz ||
                                       snapshot.step_adjustable != current.step_adjustable;
  const bool level_changed = static_cast<int>(std::lround(snapshot.relative_dbfs)) !=
                             static_cast<int>(std::lround(current.relative_dbfs));
  const bool gain_state_changed = gain_changed(snapshot, current);
  current = snapshot;
  const bool covered = gain_popup || filter_popup || mode_popup;   // a popup is over the lower half
  M5.Display.startWrite();
  if (status_changed) draw_header_status();
  if (tuner_changed) draw_band_label();
  if (covered) {
    if (mode_popup && tuner_changed) draw_mode_popup();
    if (filter_popup && (bandwidth_changed || tuner_changed)) draw_filter_popup();
    if (gain_popup && (gain_state_changed || receiver_changed)) draw_gain_popup();
    M5.Display.endWrite();
    return;
  }
  if (tuner_changed) {
    draw_frequency();
    draw_mode_chip(current.mode);
  }
  if (audio_changed && !tuning_controls_changed) draw_step_size_controls();
  if (tuning_controls_changed) draw_tuning_controls();
  if (receiver_changed) draw_footer_receiver();
  if (sample_changed) draw_footer_sample();
  if (bandwidth_changed) {
    draw_footer_bandwidth();
    if (!browser) draw_filter_panel();
  }
  if (level_changed) draw_footer_level();
  if (gain_state_changed || receiver_changed) {
    draw_gain_panel();
    draw_gain_chip();
    draw_footer_gain();
    draw_footer_bias();
  }
  M5.Display.endWrite();
}

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                   float floor, bool audio_stressed) {
  if (!shown || browser || keypad || levels == nullptr || visible_bins < 2) return;
  const uint32_t now = millis();
  const uint32_t interval = audio_stressed ? 333 : 100;
  if (now - last_spectrum_ms < interval) return;
  last_spectrum_ms = now;
  const size_t samples = std::min<size_t>(std::size(spectrum_levels), visible_bins);
  for (size_t i = 0; i < samples; ++i) {
    spectrum_levels[i] = spectrum::peak_for_pixel(
        levels, first_bin, visible_bins, i, samples);
  }
  floor = home_spectrum_floor(spectrum_levels, samples, floor);
  M5.Display.startWrite();
  M5.Display.setClipRect(kPlotX + 1, kSpectrumY + 1, kPlotW - 2, kSpectrumH - 2);
  M5.Display.fillRect(kPlotX + 1, kSpectrumY + 1, kPlotW - 2, kSpectrumH - 2,
                      TFT_BLACK);
  for (int i = 1; i < 5; ++i) {
    M5.Display.drawFastHLine(kPlotX, kSpectrumY + i * kSpectrumH / 5, kPlotW, kDim);
    M5.Display.drawFastVLine(kPlotX + i * kPlotW / 5, kSpectrumY, kSpectrumH, kDim);
  }
  floor -= kSpectrumFloorMarginDb;
  float strongest = floor;
  for (size_t i = 0; i < samples; ++i) strongest = std::max(strongest, spectrum_levels[i]);
  spectrum_ceiling = (!spectrum_ceiling_valid || strongest > spectrum_ceiling)
                         ? strongest
                         : spectrum_ceiling + (strongest - spectrum_ceiling) * 0.04f;
  spectrum_ceiling_valid = true;
  const float range_db =
      std::max(kSpectrumMinRangeDb, spectrum_ceiling + kSpectrumHeadroomDb - floor);
  const int trace_base = kSpectrumY + kSpectrumH - kSpectrumAxisBandPx;
  const int trace_height = kSpectrumH - kSpectrumAxisBandPx - 4;
  int px = kPlotX, py = trace_base;
  for (size_t i = 0; i < samples; ++i) {
    const float normalized = std::clamp((spectrum_levels[i] - floor) / range_db, 0.0f, 1.0f);
    const int x = kPlotX + static_cast<int>(i * (kPlotW - 1) / (samples - 1));
    const int y = trace_base - static_cast<int>(normalized * trace_height);
    if (i) M5.Display.drawLine(px, py, x, y, kGreen);
    px = x; py = y;
  }
  M5.Display.drawFastVLine(kPlotX + kPlotW / 2, kSpectrumY, kSpectrumH, kGreen);
  if (filter_edges && current.filter_bandwidth_hz > 0 && current.span_hz > 0) {
    // The receive filter as two lines either side of the tuned frequency.
    const int half = std::clamp<int>(
        static_cast<int>((static_cast<int64_t>(current.filter_bandwidth_hz) * kPlotW) /
                         (2 * static_cast<int64_t>(current.span_hz))),
        2, kPlotW / 2 - 2);
    M5.Display.drawFastVLine(kPlotX + kPlotW / 2 - half, kSpectrumY, kSpectrumH, TFT_YELLOW);
    M5.Display.drawFastVLine(kPlotX + kPlotW / 2 + half, kSpectrumY, kSpectrumH, TFT_YELLOW);
  }
  M5.Display.clearClipRect();
  draw_spectrum_axis();
  const int rows = waterfall_style::rows_per_frame(waterfall_style::Screen::home);
  const bool popup_over = gain_popup || filter_popup || mode_popup;
  const int waterfall_h = popup_over ? kPopY - 2 - (kWaterfallY + 1) : kWaterfallH - 2;
  M5.Display.setScrollRect(kPlotX + 1, kWaterfallY + 1, kPlotW - 2, waterfall_h, TFT_BLACK);
  M5.Display.scroll(0, -rows);
  for (size_t i = 0; i < samples; ++i) {
    const float normalized = std::clamp(
        (spectrum_levels[i] - floor) / waterfall_range_db(waterfall_contrast),
        0.0f, 1.0f);
    const int x = kPlotX + 1 + static_cast<int>(i * (kPlotW - 2) / samples);
    const int x2 = kPlotX + 1 + static_cast<int>((i + 1) * (kPlotW - 2) / samples);
    M5.Display.fillRect(x, kWaterfallY + 1 + waterfall_h - rows, std::max(1, x2 - x), rows,
                        waterfall_color(normalized));
  }
  M5.Display.endWrite();
}

Action handle_touch(int32_t x, int32_t y, bool pressed) {
  if (!shown) return {};
  if (keypad) {
    if (pressed && !gesture.down) gesture = {true, false, false, x, y, scroll_offset_px};
    if (pressed || !gesture.down) return {};
    const int32_t tap_x = gesture.start_x, tap_y = gesture.start_y;
    gesture = {};
    const auto result = freq_keypad::handle_touch(tap_x, tap_y, keypad_entry, sizeof(keypad_entry));
    if (result == freq_keypad::Result::cancelled) {
      keypad = false;
      keypad_entry[0] = '\0';
      draw_all();
    } else if (result == freq_keypad::Result::submitted) {
      char* end = nullptr;
      const double mhz = strtod(keypad_entry, &end);
      if (end != keypad_entry && *end == '\0' && mhz >= 24.0 && mhz <= 1766.0) {
        keypad = false;
        keypad_entry[0] = '\0';
        draw_all();
        return {ActionKind::tune_frequency, dashboards::Id::count,
                static_cast<uint32_t>(std::llround(mhz * 1000000.0))};
      }
    }
    return {};
  }
  if (pressed && !gesture.down) {
    gesture = {true, false, false, x, y, scroll_offset_px};
    if (!browser && inside(x, y, kListX + kListW - 18, kListY, 18, kListH) &&
        max_scroll_px() > 0)
      gesture.thumb = true;
    return {};
  }
  if (pressed && gesture.down) {
    const int dy = y - gesture.start_y;
    if (!browser && (gesture.thumb || inside(gesture.start_x, gesture.start_y,
                                              kListX, kListY, kListW, kListH))) {
      if (gesture.thumb || std::abs(dy) >= kTapDragThreshold) gesture.scrolling = true;
      if (gesture.scrolling) {
        scroll_offset_px = gesture.thumb
                               ? gesture.start_offset + dy * max_scroll_px() / kListH
                               : gesture.start_offset - dy;
        clamp_scroll();
        draw_recent_list();
      }
    }
    return {};
  }
  if (!pressed && gesture.down) {
    const bool was_scroll = gesture.scrolling;
    const int32_t tap_x = gesture.start_x, tap_y = gesture.start_y;
    gesture = {};
    if (was_scroll) return {};
    const Action action = tap_action(tap_x, tap_y);
    if (action.kind == ActionKind::open_browser) {
      browser = true;
      browser_page = 0;
      draw_browser();
      return {};
    }
    if (action.kind == ActionKind::keypad_open) {
      keypad = true;
      keypad_entry[0] = '\0';
      draw_keypad();
      return {};
    }
    if (action.kind == ActionKind::mode_open || action.kind == ActionKind::mode_close) {
      mode_popup = action.kind == ActionKind::mode_open;
      M5.Display.startWrite();
      if (mode_popup) draw_mode_popup();
      else draw_receiver_chrome(true);
      M5.Display.endWrite();
      return {};
    }
    if (action.kind == ActionKind::gain_open || action.kind == ActionKind::gain_close) {
      gain_popup = action.kind == ActionKind::gain_open;
      M5.Display.startWrite();
      if (gain_popup) draw_gain_popup();
      else draw_receiver_chrome(true);
      M5.Display.endWrite();
      return {};
    }
    if (action.kind == ActionKind::filter_open || action.kind == ActionKind::filter_close) {
      filter_popup = action.kind == ActionKind::filter_open;
      M5.Display.startWrite();
      if (filter_popup) draw_filter_popup();
      else draw_receiver_chrome(true);
      M5.Display.endWrite();
      return {};
    }
    if (action.kind == ActionKind::filter_edges) {
      filter_edges = !filter_edges;
      draw_filter_popup();
      return {};
    }
    if (action.kind == ActionKind::close_browser) {
      browser = false;
      draw_all();
      return {};
    }
    if (action.kind == ActionKind::browser_previous ||
        action.kind == ActionKind::browser_next) {
      if (action.kind == ActionKind::browser_next) ++browser_page;
      else --browser_page;
      draw_browser();
      return {};
    }
    if (action.kind == ActionKind::waterfall_palette_next) {
      waterfall_style::next_palette(waterfall_style::Screen::home);
      draw_waterfall_controls();
      return {};
    }
    if (action.kind == ActionKind::waterfall_speed_next) {
      waterfall_style::next_speed(waterfall_style::Screen::home);
      draw_waterfall_controls();
      return {};
    }
    if (action.kind == ActionKind::waterfall_contrast_down ||
        action.kind == ActionKind::waterfall_contrast_up) {
      waterfall_contrast = std::clamp<int>(
          waterfall_contrast +
              (action.kind == ActionKind::waterfall_contrast_up ? 1 : -1),
          1, 7);
      draw_waterfall_controls();
      return {};
    }
    return action;
  }
  return {};
}

bool active() { return shown; }
bool popup_open() { return shown && !browser && (gain_popup || filter_popup || mode_popup); }
int32_t list_scroll_px() { return scroll_offset_px; }
void close_popup() {
  if (!popup_open()) return;
  gain_popup = filter_popup = mode_popup = false;
  M5.Display.startWrite();
  draw_receiver_chrome(true);
  M5.Display.endWrite();
}
bool browser_active() { return shown && browser; }
bool keypad_open() { return shown && !browser && keypad; }

bool self_check() {
  const float levels[] = {60.0f, 60.0f, 60.0f, 84.0f};
  const size_t browser_pages = (dashboards::count() + kBrowserPageSize - 1) /
                               kBrowserPageSize;
  const bool saved_browser = browser;
  const size_t saved_page = browser_page;
  browser = true;
  browser_page = 0;
  const bool first_page_ok =
      tap_action(kBrowserCardX + 1, kBrowserCardY + 1).dashboard == dashboards::Id::fm &&
      tap_action(685, kBrowserNavY + 1).kind == ActionKind::browser_next;
  browser_page = 1;
  const bool second_page_ok =
      tap_action(kBrowserCardX + 1, kBrowserCardY + 1).dashboard == dashboards::Id::rf_lab &&
      tap_action(487, kBrowserNavY + 1).kind == ActionKind::browser_previous;
  browser = saved_browser;
  browser_page = saved_page;
  return dashboards::self_check() && first_page_ok && second_page_ok &&
         kVisibleRows == 7 && kTapDragThreshold == 10 &&
         dashboards::count() > static_cast<size_t>(kVisibleRows) &&
         browser_pages == 2 &&
         kBrowserCardY + static_cast<int>(kBrowserRows - 1) * kBrowserRowPitch +
                 kBrowserCardH <= kBrowserNavY &&
         kBrowserNavY + 44 < 710 &&
         kHeaderStatusX + kHeaderStatusW <= 1099 &&
         waterfall_range_db(1) == 48 && waterfall_range_db(7) == 12 &&
         step_span(2400000, -1) == 1200000 &&
         step_span(1200000, -1) == 960000 &&
         step_span(960000, 1) == 1200000 &&
         step_span(120000, 1) == 240000 &&
         tap_action(kContrastDownX + 1, kContrastY + 1).kind ==
             ActionKind::waterfall_contrast_down &&
         tap_action(kContrastUpX + 1, kContrastY + 1).kind ==
             ActionKind::waterfall_contrast_up &&
         tap_action(kStepperSpan + 5, kControlY + 8).kind == ActionKind::span_down &&
         tap_action(kStepperTune + kStepperW - 5, kControlY + 8).kind == ActionKind::step_up &&
         tap_action(kStepperSize + 5, kControlY + 8).kind == ActionKind::step_size_down &&
         tap_action(1223, 13).kind == ActionKind::open_device_settings &&
         tap_action(1100, 13).kind == ActionKind::sound_toggle &&
         std::abs(home_spectrum_floor(levels, std::size(levels), 10.0f) - 62.0f) < 0.01f;
}

}  // namespace orcsdr::home

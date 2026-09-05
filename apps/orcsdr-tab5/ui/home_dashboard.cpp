#include "home_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "orc_badge.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::home {
namespace {

constexpr uint16_t kCyan = 0x05FF;
constexpr uint16_t kGreen = 0x6FE0;
constexpr uint16_t kDim = 0x4228;
constexpr uint16_t kPanel = 0x0021;
constexpr int kRailX = 24, kRailY = 112, kRailW = 280, kRailH = 530;
constexpr int kMainX = 318, kMainY = 112, kMainW = 930, kMainH = 530;
constexpr int kPlotX = 330, kPlotW = 906;
constexpr int kSpectrumY = 144, kSpectrumH = 151;
constexpr int kWaterfallY = 298, kWaterfallH = 158;
constexpr int kContrastY = 462;
constexpr int kContrastDownX = 462;
constexpr int kContrastUpX = 724;
constexpr int kContrastButtonW = 36;
constexpr int kContrastButtonH = 28;
constexpr int kListX = 30, kListY = 158, kListW = 242, kListH = 420;
constexpr int kRowH = 52, kRowGap = 8, kRowPitch = kRowH + kRowGap;
constexpr int kVisibleRows = 7;
constexpr int kAllY = 590;
constexpr int kTapDragThreshold = 10;
constexpr int kHeaderStatusX = 595;
constexpr int kHeaderStatusW = 474;

Snapshot current{};
bool shown = false;
bool browser = false;
int32_t scroll_offset_px = 0;
uint32_t last_spectrum_ms = 0;
float spectrum_levels[256]{};
uint8_t waterfall_contrast = 5;

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

void panel(int x, int y, int w, int h, uint16_t color = kCyan, int radius = 10) {
  M5.Display.fillRoundRect(x, y, w, h, radius, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, radius, color);
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

void draw_battery(int x, int y) {
  M5.Display.drawRoundRect(x, y, 58, 27, 4, TFT_WHITE);
  M5.Display.fillRect(x + 58, y + 8, 5, 11, TFT_WHITE);
  const int fill = current.battery_percent < 0
                       ? 0
                       : std::clamp<int32_t>(current.battery_percent, 0, 100) / 2;
  M5.Display.fillRect(x + 4, y + 4, fill, 19,
                      current.battery_percent >= 20 ? kGreen : TFT_ORANGE);
}

void draw_menu_icon(dashboards::Id id, int x, int y, uint16_t color) {
  if (id == dashboards::Id::home) {
    M5.Display.fillTriangle(x - 14, y, x, y - 13, x + 14, y, color);
    M5.Display.drawRect(x - 10, y, 20, 15, color);
  } else if (id == dashboards::Id::fm) {
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

void draw_header_status() {
  M5.Display.fillRect(kHeaderStatusX, 20, kHeaderStatusW, 72, TFT_BLACK);
  panel(kHeaderStatusX, 20, kHeaderStatusW, 72, kCyan, 9);
  draw_wifi_icon(kHeaderStatusX + 26, 55, current.wifi_connected ? kCyan : kDim);
  text("Wi-Fi", kHeaderStatusX + 54, 42, TFT_WHITE, 2);
  text(current.wifi_connected && current.wifi_ip[0] ? current.wifi_ip : "OFFLINE",
       kHeaderStatusX + 54, 66, current.wifi_connected ? kCyan : TFT_ORANGE, 1);
  M5.Display.drawFastVLine(kHeaderStatusX + 114, 30, 52, kDim);
  draw_usb_icon(kHeaderStatusX + 135, 54, current.usb_connected ? kCyan : kDim);
  text("USB", kHeaderStatusX + 158, 42, TFT_WHITE, 2);
  text(current.usb_connected ? "CONNECTED" : "DISCONNECTED", kHeaderStatusX + 158, 66,
       current.usb_connected ? kCyan : TFT_ORANGE, 1);
  M5.Display.drawFastVLine(kHeaderStatusX + 238, 30, 52, kDim);
  draw_battery(kHeaderStatusX + 254, 39);
  char value[12];
  snprintf(value, sizeof(value), "%ld%%", static_cast<long>(current.battery_percent));
  text(current.battery_percent >= 0 ? value : "--", kHeaderStatusX + 322, 54, TFT_WHITE, 2);
  M5.Display.drawFastVLine(kHeaderStatusX + 352, 30, 52, kDim);
  text(current.clock[0] ? current.clock : "--:--", kHeaderStatusX + kHeaderStatusW - 12, 42, TFT_WHITE, 2,
       middle_right);
  text(current.date[0] ? current.date : "UPTIME", kHeaderStatusX + kHeaderStatusW - 12, 68, kCyan, 2,
       middle_right);
}

void draw_header() {
  if (!badge::draw(24, 12, 96))
    M5.Display.drawRoundRect(24, 12, 96, 96, 12, kGreen);
  text("OrcSDR", 132, 46, kGreen, 4);
  text("M5STACK TAB5", 134, 82, kCyan, 2);
  M5.Display.drawFastVLine(306, 22, 76, kCyan);
  text("HOME", 338, 59, TFT_WHITE, 5);
  draw_header_status();
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
  panel(kRailX, kRailY, kRailW, kRailH, kCyan, 12);
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
  value = std::clamp(value, 0.0f, 1.0f);
  const uint8_t r = value > 0.62f
                        ? static_cast<uint8_t>(std::min(255.0f, (value - 0.62f) * 670))
                        : 0;
  const uint8_t g = value > 0.25f
                        ? static_cast<uint8_t>(std::min(255.0f, (value - 0.25f) * 520))
                        : 0;
  const uint8_t b = static_cast<uint8_t>(35 + (1.0f - value) * 150);
  return M5.Display.color565(r, g, b);
}

uint8_t waterfall_range_db(uint8_t contrast) {
  return static_cast<uint8_t>(54 - std::clamp<uint8_t>(contrast, 1, 7) * 6);
}

void draw_waterfall_controls() {
  M5.Display.fillRect(kContrastDownX, kContrastY, 298, kContrastButtonH, TFT_BLACK);
  panel(kContrastDownX, kContrastY, kContrastButtonW, kContrastButtonH, kCyan, 5);
  text("<", kContrastDownX + kContrastButtonW / 2, kContrastY + 14, kGreen, 2,
       middle_center);
  char value[24];
  snprintf(value, sizeof(value), "WF CONTRAST %u", waterfall_contrast);
  text(value, 615, kContrastY + 14, kCyan, 2, middle_center);
  panel(kContrastUpX, kContrastY, kContrastButtonW, kContrastButtonH, kCyan, 5);
  text(">", kContrastUpX + kContrastButtonW / 2, kContrastY + 14, kGreen, 2,
       middle_center);
}

float home_spectrum_floor(const float* levels, size_t count, float pipeline_floor) {
  float sum = 0.0f;
  for (size_t i = 0; i < count; ++i) sum += levels[i];
  return std::max(pipeline_floor, sum / static_cast<float>(count) - 4.0f);
}

void draw_frequency() {
  M5.Display.fillRect(kPlotX, 458, 430, 88, TFT_BLACK);
  text("FREQUENCY", kPlotX, 472, kCyan, 2);
  char value[40];
  snprintf(value, sizeof(value), current.frequency_hz >= 1000000 ? "%.3f" : "%.1f",
           current.frequency_hz >= 1000000 ? current.frequency_hz / 1000000.0
                                           : current.frequency_hz / 1000.0);
  text(value, kPlotX + 40, 516, kGreen, 4);
  text(current.frequency_hz >= 1000000 ? "MHz" : "kHz", kPlotX + 350, 526,
       kGreen, 2);
  if (current.requested_frequency_hz != 0 &&
      current.requested_frequency_hz != current.frequency_hz)
    text("TUNING", kPlotX + 425, 526, TFT_YELLOW, 1, middle_right);
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

void draw_tuning_controls() {
  M5.Display.fillRect(kPlotX, 564, 610, 62, TFT_BLACK);
  panel(kPlotX, 564, 610, 62, kCyan, 9);
  panel(338, 571, 60, 48, kCyan, 7); text("<", 368, 595, kGreen, 3, middle_center);
  text("SPAN", 470, 578, kCyan, 2, middle_center);
  char value[24];
  snprintf(value, sizeof(value), "%.0f kHz", current.span_hz / 1000.0);
  text(value, 470, 606, kGreen, 2, middle_center);
  panel(548, 571, 60, 48, kCyan, 7); text(">", 578, 595, kGreen, 3, middle_center);
  panel(640, 571, 60, 48, kCyan, 7); text("<", 670, 595, kGreen, 3, middle_center);
  text("STEP", 780, 578, kCyan, 2, middle_center);
  snprintf(value, sizeof(value), "%.1f kHz", current.step_hz / 1000.0);
  text(value, 780, 606, kGreen, 2, middle_center);
  panel(864, 571, 60, 48, kCyan, 7); text(">", 894, 595, kGreen, 3, middle_center);
}

void draw_audio_controls() {
  M5.Display.fillRect(998, 468, 236, 72, TFT_BLACK);
  panel(998, 468, 70, 72, kCyan, 8);
  text("-", 1033, 504, kGreen, 4, middle_center);
  panel(1076, 468, 80, 72, kCyan, 8);
  text(current.sound_enabled ? "VOL" : "MUTE", 1116, 486,
       current.sound_enabled ? kCyan : TFT_ORANGE, 2, middle_center);
  char value[8];
  snprintf(value, sizeof(value), "%u", current.volume);
  text(value, 1116, 515, current.sound_enabled ? kGreen : TFT_LIGHTGREY, 2,
       middle_center);
  panel(1164, 468, 70, 72, kCyan, 8);
  text("+", 1199, 504, kGreen, 4, middle_center);
}

void footer_text(const char* value, int x, uint16_t color) {
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color, kPanel);
  M5.Display.setTextSize(2);
  M5.Display.drawString(value, x, 678);
}

void draw_footer_receiver() {
  M5.Display.fillRect(30, 660, 136, 36, kPanel);
  footer_text("RTL-SDR v4", 98, current.driver_ready ? kGreen : TFT_ORANGE);
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
  panel(24, 654, 1224, 48, kCyan, 8);
  for (const int x : {170, 384, 560, 700, 856})
    M5.Display.drawFastVLine(x, 662, 32, kDim);
  footer_text("GAIN AUTO", 630, kGreen);
  footer_text("BIAS N/A", 778, TFT_LIGHTGREY);
  draw_footer_receiver();
  draw_footer_sample();
  draw_footer_bandwidth();
  draw_footer_level();
}

void draw_receiver_chrome() {
  panel(kMainX, kMainY, kMainW, kMainH, kCyan, 12);
  text("SPECTRUM", kPlotX, 130, kCyan, 2);
  text(current.receiving ? "LIVE" : "READY", 1016, 130,
       current.receiving ? kGreen : TFT_ORANGE, 1);
  panel(1064, 118, 108, 26, kCyan, 6);
  text("GAIN  AUTO", 1118, 131, kGreen, 1, middle_center);
  M5.Display.fillRect(kPlotX, kSpectrumY, kPlotW, kSpectrumH, TFT_BLACK);
  M5.Display.drawRect(kPlotX, kSpectrumY, kPlotW, kSpectrumH, kDim);
  for (int i = 1; i < 5; ++i) {
    M5.Display.drawFastHLine(kPlotX, kSpectrumY + i * kSpectrumH / 5, kPlotW, kDim);
    M5.Display.drawFastVLine(kPlotX + i * kPlotW / 5, kSpectrumY, kSpectrumH, kDim);
  }
  M5.Display.fillRect(kPlotX, kWaterfallY, kPlotW, kWaterfallH, TFT_BLACK);
  M5.Display.drawRect(kPlotX, kWaterfallY, kPlotW, kWaterfallH, kDim);
  draw_frequency();
  panel(770, 468, 92, 72, kCyan, 8);
  text("MODE", 816, 484, kCyan, 2, middle_center);
  text(current.mode[0] ? current.mode : "--", 816, 516, kGreen, 2, middle_center);
  panel(874, 468, 112, 72, kCyan, 8);
  text("STEP", 930, 484, kCyan, 2, middle_center);
  char value[24]; snprintf(value, sizeof(value), "%.1f kHz", current.step_hz / 1000.0);
  text(value, 930, 516, kGreen, 2, middle_center);
  draw_audio_controls();
  draw_tuning_controls();
  panel(950, 564, 128, 62, kCyan, 7);
  text("FILTER", 1014, 582, kCyan, 2, middle_center);
  snprintf(value, sizeof(value), "%lu kHz", static_cast<unsigned long>(current.filter_bandwidth_hz / 1000u));
  text(current.filter_bandwidth_hz ? value : "AUTO", 1014, 608, kGreen, 2, middle_center);
  panel(1090, 564, 144, 62, kCyan, 7);
  text("SIGNAL", 1162, 582, kCyan, 2, middle_center);
  snprintf(value, sizeof(value), "%.1f dBFS", static_cast<double>(current.relative_dbfs));
  text(value, 1162, 608, kGreen, 2, middle_center);
}

void draw_browser() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.drawRoundRect(10, 10, 1260, 700, 14, kCyan);
  text("ALL DASHBOARDS", 38, 48, TFT_WHITE, 4);
  panel(1072, 20, 120, 54, kCyan, 8);
  text("HOME", 1132, 47, kCyan, 2, middle_center);
  audio_header::draw_settings_button();
  audio_header::draw_mute_button(current.sound_enabled);
  audio_header::draw_visualizer_button(current.receiving);
  for (size_t i = 0; i < dashboards::count(); ++i) {
    const auto* entry = dashboards::descriptor(i);
    if (!entry) continue;
    const int col = static_cast<int>(i % 3), row = static_cast<int>(i / 3);
    const int x = 30 + col * 410, y = 104 + row * 140;
    panel(x, y, 390, 118, entry->available ? kCyan : kDim, 10);
    draw_menu_icon(entry->id, x + 42, y + 46, entry->available ? kCyan : kDim);
    text(entry->title, x + 78, y + 38, entry->available ? TFT_WHITE : kDim, 2);
    text(entry->subtitle, x + 78, y + 70, entry->available ? TFT_LIGHTGREY : kDim, 1);
  }
}

void draw_all() {
  // Dashboards may select custom M5GFX fonts; Home owns the built-in font.
  M5.Display.setFont(nullptr);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.drawRoundRect(10, 10, 1260, 700, 14, kCyan);
  draw_header();
  draw_rail();
  draw_receiver_chrome();
  draw_footer();
}

Action tap_action(int32_t x, int32_t y) {
  if (audio_header::settings_hit(x, y))
    return {ActionKind::open_device_settings};
  if (audio_header::mute_hit(x, y)) return {ActionKind::sound_toggle};
  if (browser) {
    if (inside(x, y, 1072, 20, 120, 54)) return {ActionKind::close_browser};
    for (size_t i = 0; i < dashboards::count(); ++i) {
      const int col = static_cast<int>(i % 3), row = static_cast<int>(i / 3);
      if (inside(x, y, 30 + col * 410, 104 + row * 140, 390, 118)) {
        const auto* entry = dashboards::descriptor(i);
        if (entry && entry->available)
          return {ActionKind::open_dashboard, entry->id};
      }
    }
    return {};
  }
  if (inside(x, y, kContrastDownX, kContrastY, kContrastButtonW, kContrastButtonH))
    return {ActionKind::waterfall_contrast_down};
  if (inside(x, y, kContrastUpX, kContrastY, kContrastButtonW, kContrastButtonH))
    return {ActionKind::waterfall_contrast_up};
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
  if (inside(x, y, 338, 571, 60, 48)) return {ActionKind::span_down};
  if (inside(x, y, 548, 571, 60, 48)) return {ActionKind::span_up};
  if (inside(x, y, 640, 571, 60, 48)) return {ActionKind::step_down};
  if (inside(x, y, 864, 571, 60, 48)) return {ActionKind::step_up};
  if (inside(x, y, 998, 468, 70, 72)) return {ActionKind::volume_down};
  if (inside(x, y, 1076, 468, 80, 72)) return {ActionKind::sound_toggle};
  if (inside(x, y, 1164, 468, 70, 72)) return {ActionKind::volume_up};
  return {};
}

}  // namespace

void enter(const Snapshot& snapshot) {
  current = snapshot;
  shown = true;
  browser = false;
  gesture = {};
  last_spectrum_ms = 0;
  clamp_scroll();
  draw_all();
}

void leave() { shown = browser = false; gesture = {}; }

void draw() {
  if (!shown) return;
  if (browser) draw_browser();
  else draw_all();
}

void update(const Snapshot& snapshot) {
  if (!shown || browser || snapshot.revision == current.revision) return;
  const bool tuner_changed = snapshot.tuner_revision != current.tuner_revision;
  const bool audio_changed = snapshot.audio_revision != current.audio_revision;
  const bool status_changed = snapshot.status_revision != current.status_revision;
  const bool receiver_changed = snapshot.driver_ready != current.driver_ready;
  const bool sample_changed = snapshot.effective_sps / 1000u != current.effective_sps / 1000u;
  const bool bandwidth_changed =
      snapshot.filter_bandwidth_hz != current.filter_bandwidth_hz;
  const bool tuning_controls_changed = snapshot.span_hz != current.span_hz ||
                                       snapshot.step_hz != current.step_hz;
  const bool level_changed = static_cast<int>(std::lround(snapshot.relative_dbfs)) !=
                             static_cast<int>(std::lround(current.relative_dbfs));
  current = snapshot;
  M5.Display.startWrite();
  if (tuner_changed) {
    draw_frequency();
    M5.Display.fillRect(770, 468, 216, 72, TFT_BLACK);
    panel(770, 468, 92, 72, kCyan, 8);
    text("MODE", 816, 484, kCyan, 2, middle_center);
    text(current.mode, 816, 516, kGreen, 2, middle_center);
    panel(874, 468, 112, 72, kCyan, 8);
    char value[24]; snprintf(value, sizeof(value), "%.1f kHz", current.step_hz / 1000.0);
    text("STEP", 930, 484, kCyan, 2, middle_center);
    text(value, 930, 516, kGreen, 2, middle_center);
  }
  if (audio_changed) draw_audio_controls();
  if (tuning_controls_changed) draw_tuning_controls();
  if (status_changed) draw_header_status();
  if (receiver_changed) draw_footer_receiver();
  if (sample_changed) draw_footer_sample();
  if (bandwidth_changed) draw_footer_bandwidth();
  if (level_changed) draw_footer_level();
  M5.Display.endWrite();
}

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                   float floor, bool audio_stressed) {
  if (!shown || browser || levels == nullptr || visible_bins < 2) return;
  const uint32_t now = millis();
  const uint32_t interval = audio_stressed ? 333 : 100;
  if (now - last_spectrum_ms < interval) return;
  last_spectrum_ms = now;
  const size_t samples = std::min<size_t>(256, visible_bins);
  for (size_t i = 0; i < samples; ++i) {
    const size_t source = first_bin + i * visible_bins / samples;
    spectrum_levels[i] = levels[source];
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
  int px = kPlotX, py = kSpectrumY + kSpectrumH - 2;
  for (size_t i = 0; i < samples; ++i) {
    const float normalized = std::clamp((spectrum_levels[i] - floor) / 24.0f, 0.0f, 1.0f);
    const int x = kPlotX + static_cast<int>(i * (kPlotW - 1) / (samples - 1));
    const int y = kSpectrumY + kSpectrumH - 2 - static_cast<int>(normalized * (kSpectrumH - 4));
    if (i) M5.Display.drawLine(px, py, x, y, kGreen);
    px = x; py = y;
  }
  M5.Display.drawFastVLine(kPlotX + kPlotW / 2, kSpectrumY, kSpectrumH, kGreen);
  M5.Display.clearClipRect();
  draw_spectrum_axis();
  M5.Display.setScrollRect(kPlotX + 1, kWaterfallY + 1, kPlotW - 2,
                           kWaterfallH - 2, TFT_BLACK);
  M5.Display.scroll(0, -2);
  for (size_t i = 0; i < samples; ++i) {
    const float normalized = std::clamp(
        (spectrum_levels[i] - floor) / waterfall_range_db(waterfall_contrast),
        0.0f, 1.0f);
    const int x = kPlotX + 1 + static_cast<int>(i * (kPlotW - 2) / samples);
    const int x2 = kPlotX + 1 + static_cast<int>((i + 1) * (kPlotW - 2) / samples);
    M5.Display.fillRect(x, kWaterfallY + kWaterfallH - 3, std::max(1, x2 - x), 2,
                        waterfall_color(normalized));
  }
  M5.Display.endWrite();
}

Action handle_touch(int32_t x, int32_t y, bool pressed) {
  if (!shown) return {};
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
      draw_browser();
      return {};
    }
    if (action.kind == ActionKind::close_browser) {
      browser = false;
      draw_all();
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
bool browser_active() { return shown && browser; }

bool self_check() {
  const float levels[] = {60.0f, 60.0f, 60.0f, 84.0f};
  return dashboards::self_check() && kVisibleRows == 7 && kTapDragThreshold == 10 &&
         dashboards::count() > static_cast<size_t>(kVisibleRows) &&
         kHeaderStatusX + kHeaderStatusW <= 1099 &&
         waterfall_range_db(1) == 48 && waterfall_range_db(7) == 12 &&
         tap_action(kContrastDownX + 1, kContrastY + 1).kind ==
             ActionKind::waterfall_contrast_down &&
         tap_action(kContrastUpX + 1, kContrastY + 1).kind ==
             ActionKind::waterfall_contrast_up &&
         tap_action(339, 572).kind == ActionKind::span_down &&
         tap_action(865, 572).kind == ActionKind::step_up &&
         tap_action(999, 469).kind == ActionKind::volume_down &&
         tap_action(1223, 13).kind == ActionKind::open_device_settings &&
         tap_action(1100, 13).kind == ActionKind::sound_toggle &&
         std::abs(home_spectrum_floor(levels, std::size(levels), 10.0f) - 62.0f) < 0.01f;
}

}  // namespace orcsdr::home

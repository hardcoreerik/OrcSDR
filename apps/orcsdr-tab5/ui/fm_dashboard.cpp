#include "fm_dashboard.hpp"

#include "dashboard_audio_control.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::fm {
namespace {

extern const uint8_t orc_badge_start[] asm("_binary_orc_badge_104_png_start");
extern const uint8_t orc_badge_end[] asm("_binary_orc_badge_104_png_end");

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr int kHeaderH = 132;
constexpr int kTabsY = 630;
constexpr int kTabW = 256;
constexpr uint32_t kFmMinHz = 88000000;
constexpr uint32_t kFmMaxHz = 108000000;
constexpr int kSpectrumX = 46;
constexpr int kSpectrumY = 246;
constexpr int kSpectrumW = 1188;
constexpr int kSpectrumH = 145;
constexpr int kWaterfallY = 420;
constexpr int kWaterfallH = 130;

static_assert(static_cast<uint8_t>(View::count) == 5);

Snapshot g_snapshot{};
View g_view = View::listen;
bool g_active = false;
bool g_keypad = false;
audio_header::Control g_audio_control{};
char g_entry[12]{};
uint32_t g_last_dynamic_ms = 0;
uint16_t g_waterfall_row[kSpectrumW]{};

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          int size = 2, textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void card(int x, int y, int w, int h) {
  M5.Display.fillRoundRect(x, y, w, h, 12, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 12, kCyan);
}

void label(const char* value, int x, int y) {
  text(value, x, y, kCyan, 2, top_left);
}

void button(int x, int y, int w, int h, const char* title, uint16_t color = kCyan,
            bool selected = false) {
  const uint16_t fill = selected ? 0x1264 : kPanel;
  M5.Display.fillRoundRect(x, y, w, h, 10, fill);
  M5.Display.drawRoundRect(x, y, w, h, 10, color);
  text(title, x + w / 2, y + h / 2, selected ? color : TFT_WHITE, 2);
}

void draw_radio_icon(int cx, int cy, uint16_t color) {
  M5.Display.drawRoundRect(cx - 28, cy - 20, 56, 42, 8, color);
  M5.Display.drawCircle(cx + 10, cy + 2, 10, color);
  M5.Display.drawLine(cx - 18, cy - 10, cx + 12, cy - 10, color);
  M5.Display.drawLine(cx - 18, cy - 2, cx - 5, cy - 2, color);
  M5.Display.drawLine(cx - 18, cy + 6, cx - 5, cy + 6, color);
  M5.Display.drawLine(cx - 22, cy - 22, cx + 20, cy - 38, color);
}

void draw_gear(int cx, int cy, uint16_t color) {
  M5.Display.drawCircle(cx, cy, 20, color);
  M5.Display.drawCircle(cx, cy, 7, color);
  for (int i = 0; i < 8; ++i) {
    const float a = i * 3.14159265f / 4.0f;
    M5.Display.drawLine(cx + static_cast<int>(cosf(a) * 21),
                        cy + static_cast<int>(sinf(a) * 21),
                        cx + static_cast<int>(cosf(a) * 29),
                        cy + static_cast<int>(sinf(a) * 29), color);
  }
}

void draw_header() {
  M5.Display.fillRect(0, 0, 1280, kHeaderH, kBg);
  M5.Display.drawFastHLine(8, kHeaderH - 1, 1264, kCyan);
  const size_t badge_size = static_cast<size_t>(orc_badge_end - orc_badge_start);
  if (!M5.Display.drawPng(orc_badge_start, badge_size, 18, 13)) {
    M5.Display.drawRoundRect(18, 13, 104, 104, 18, kGreen);
  }
  text("OrcSDR", 142, 38, TFT_WHITE, 4, middle_left);
  text("FM Broadcast", 142, 82, kCyan, 2, middle_left);
  M5.Display.drawFastVLine(365, 25, 82, kCyan);
  draw_radio_icon(456, 70, kCyan);
  text("FM Broadcast", 530, 66, TFT_WHITE, 4, middle_left);
  M5.Display.drawFastVLine(865, 25, 82, kCyan);
  audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                     g_snapshot.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_settings_button();
}

void draw_tab_icon(View view, int cx, int cy, uint16_t color) {
  if (view == View::listen) draw_radio_icon(cx, cy, color);
  else if (view == View::spectrum) {
    for (int i = 0; i < 6; ++i)
      M5.Display.fillRect(cx - 24 + i * 9, cy + 14 - (i % 3 + 1) * 9, 5,
                          (i % 3 + 1) * 9, color);
  } else if (view == View::station_rds) {
    M5.Display.drawRoundRect(cx - 25, cy - 18, 50, 34, 5, color);
    M5.Display.drawLine(cx - 10, cy + 16, cx - 18, cy + 25, color);
    M5.Display.drawFastHLine(cx - 15, cy - 7, 30, color);
    M5.Display.drawFastHLine(cx - 15, cy + 2, 20, color);
  } else if (view == View::rf_health) {
    const int16_t xs[] = {-28, -18, -10, -3, 5, 12, 20, 29};
    const int16_t ys[] = {0, 0, -18, 20, -24, 12, 0, 0};
    for (int i = 1; i < 8; ++i)
      M5.Display.drawLine(cx + xs[i - 1], cy + ys[i - 1], cx + xs[i], cy + ys[i], color);
  } else draw_gear(cx, cy, color);
}

void draw_tabs() {
  static constexpr const char* names[] = {
      "LISTEN", "SPECTRUM", "STATION / RDS", "RF HEALTH", "SETTINGS"};
  for (uint8_t i = 0; i < static_cast<uint8_t>(View::count); ++i) {
    const int x = i * kTabW;
    const bool selected = i == static_cast<uint8_t>(g_view);
    M5.Display.fillRect(x, kTabsY, kTabW, 90, selected ? 0x0a43 : kPanel);
    M5.Display.drawRect(x, kTabsY, kTabW, 90, selected ? kCyan : kGrid);
    draw_tab_icon(static_cast<View>(i), x + 52, kTabsY + 43,
                  selected ? (i == 0 ? kGreen : kCyan) : TFT_WHITE);
    text(names[i], x + 92, kTabsY + 45, selected ? kCyan : TFT_WHITE, 2, middle_left);
  }
}

void draw_segment_meter(int x, int y, int w, float dbfs, int segments = 18) {
  const float normalized = std::clamp((dbfs + 40.0f) / 40.0f, 0.0f, 1.0f);
  const int lit = static_cast<int>(normalized * segments);
  const int gap = 3;
  const int sw = (w - (segments - 1) * gap) / segments;
  for (int i = 0; i < segments; ++i) {
    const uint16_t color = i < lit ? (i >= segments - 4 ? kYellow : kGreen) : kGrid;
    M5.Display.fillRect(x + i * (sw + gap), y, sw, 32, color);
  }
}

int relative_percent() {
  return std::clamp(static_cast<int>(lroundf((g_snapshot.relative_dbfs + 90.0f) *
                                             (100.0f / 90.0f))), 0, 100);
}

void draw_listen_static() {
  card(24, 160, 300, 140);
  label("PRESET", 44, 177);
  card(24, 316, 300, 145);
  label("RELATIVE LEVEL", 44, 333);
  card(942, 160, 314, 301);
  label("STEREO VU", 962, 177);
  const int widths[] = {220, 220, 280, 220, 220};
  const char* names[] = {"<<  SEEK -", "<  STEP -", "ENTER FREQUENCY", "STEP +  >", "SEEK +  >>"};
  int x = 24;
  for (int i = 0; i < 5; ++i) {
    button(x, 485, widths[i], 120, names[i], i == 2 ? kCyan : kGrid, i == 2);
    x += widths[i] + 10;
  }
}

void draw_listen_dynamic() {
  M5.Display.fillRect(42, 211, 264, 73, kPanel);
  char value[32];
  snprintf(value, sizeof(value), "%02u", g_snapshot.preset_index);
  text(value, 45, 248, TFT_WHITE, 5, middle_left);
  text(g_snapshot.preset_count ? "*" : "+", 270, 248, kGreen, 5);

  M5.Display.fillRect(42, 370, 264, 74, kPanel);
  draw_segment_meter(45, 371, 245, g_snapshot.relative_dbfs, 10);
  snprintf(value, sizeof(value), "%d%%", relative_percent());
  text(value, 45, 429, kGreen, 3, middle_left);
  text(relative_percent() >= 60 ? "Good" : relative_percent() >= 30 ? "Fair" : "Weak",
       290, 429, TFT_WHITE, 2, middle_right);

  M5.Display.fillRect(340, 145, 580, 320, kBg);
  snprintf(value, sizeof(value), "%.1f", g_snapshot.frequency_hz / 1000000.0);
  text(value, 615, 235, TFT_WHITE, 8);
  text("MHz", 850, 252, TFT_WHITE, 4);
  const char* ps = g_snapshot.program_service[0] ? g_snapshot.program_service : "—";
  text(ps, 630, 335, TFT_WHITE, 4);
  const char* rt = g_snapshot.radio_text[0] ? g_snapshot.radio_text : "RDS text unavailable";
  text(rt, 630, 382, TFT_WHITE, 2);
  button(455, 415, 175, 44, g_snapshot.running ? "RUNNING" : "STOPPED",
         g_snapshot.running ? kGreen : TFT_RED, true);
  button(680, 415, 160, 44, g_snapshot.stereo ? "STEREO" : "MONO",
         g_snapshot.stereo ? kGreen : kMuted);

  M5.Display.fillRect(960, 220, 275, 210, kPanel);
  text("L", 964, 270, TFT_WHITE, 3, middle_left);
  draw_segment_meter(1000, 255, 220, g_snapshot.left_dbfs, 14);
  text("R", 964, 370, TFT_WHITE, 3, middle_left);
  draw_segment_meter(1000, 355, 220, g_snapshot.right_dbfs, 14);
}

void draw_spectrum_static() {
  card(24, 140, 1232, 470);
  label("CENTER FREQUENCY", 46, 158);
  label("DSP FILTER BW", 548, 158);
  label("IQ ACTIVITY", 922, 158);
  for (int i = 0; i <= 4; ++i) {
    M5.Display.drawFastVLine(kSpectrumX + i * kSpectrumW / 4, kSpectrumY,
                             kSpectrumH, kGrid);
    if (i > 0) M5.Display.drawFastHLine(kSpectrumX, kSpectrumY + i * kSpectrumH / 4,
                                        kSpectrumW, kGrid);
  }
  M5.Display.drawRect(kSpectrumX, kWaterfallY, kSpectrumW, kWaterfallH, kCyan);
  M5.Display.setScrollRect(kSpectrumX + 1, kWaterfallY + 1, kSpectrumW - 2,
                           kWaterfallH - 2, kBg);
  button(390, 565, 70, 42, "-", kCyan);
  button(820, 565, 70, 42, "+", kCyan);
  text("SPAN", 55, 585, kCyan, 2, middle_left);
  text("TAP SPECTRUM TO TUNE", 1040, 585, TFT_WHITE, 2);
}

void draw_spectrum_dynamic() {
  char value[32];
  M5.Display.fillRect(45, 185, 390, 58, kPanel);
  snprintf(value, sizeof(value), "%.1f MHz", g_snapshot.frequency_hz / 1000000.0);
  text(value, 50, 215, TFT_WHITE, 5, middle_left);
  M5.Display.fillRect(545, 185, 220, 58, kPanel);
  snprintf(value, sizeof(value), "%lu kHz",
           static_cast<unsigned long>(g_snapshot.filter_bandwidth_hz / 1000));
  text(value, 655, 215, kGreen, 4);
  M5.Display.fillRect(920, 185, 300, 58, kPanel);
  text("L", 930, 201, TFT_WHITE, 2, middle_left);
  draw_segment_meter(960, 188, 245, g_snapshot.left_dbfs, 14);
  text("R", 930, 233, TFT_WHITE, 2, middle_left);
  draw_segment_meter(960, 220, 245, g_snapshot.right_dbfs, 14);
  snprintf(value, sizeof(value), "%.1f MHz", g_snapshot.span_hz / 1000000.0);
  M5.Display.fillRect(125, 565, 210, 42, kPanel);
  text(value, 220, 585, TFT_WHITE, 3);
}

void draw_station_static() {
  card(24, 140, 1232, 125);
  card(24, 280, 280, 330);
  label("NOW PLAYING", 44, 298);
  card(320, 280, 620, 330);
  label("RDS INFORMATION", 340, 298);
  card(956, 280, 300, 98);
  label("STEREO STATUS", 976, 298);
  card(956, 390, 300, 98);
  label("PILOT STATUS", 976, 408);
  card(956, 500, 300, 110);
  label("DECODER STATUS", 976, 518);
  const char* tags[] = {"PS", "RT", "PI", "PTY"};
  const int ys[] = {345, 415, 495, 565};
  for (int i = 0; i < 4; ++i) {
    M5.Display.drawRoundRect(340, ys[i] - 25, 58, 52, 8, kCyan);
    text(tags[i], 369, ys[i], kCyan, 3);
    if (i < 3) M5.Display.drawFastHLine(410, ys[i] + 31, 505, kGrid);
  }
}

void draw_station_dynamic() {
  char value[40];
  M5.Display.fillRect(44, 159, 1170, 86, kPanel);
  snprintf(value, sizeof(value), "%.1f MHz", g_snapshot.frequency_hz / 1000000.0);
  text(value, 55, 202, TFT_WHITE, 6, middle_left);
  text(g_snapshot.program_service[0] ? g_snapshot.program_service : "—",
       575, 190, TFT_WHITE, 4, middle_left);
  text(g_snapshot.radio_text[0] ? g_snapshot.radio_text : "RDS station data unavailable",
       575, 226, TFT_WHITE, 2, middle_left);

  M5.Display.fillRect(45, 335, 238, 250, kPanel);
  for (int i = 0; i < 11; ++i) {
    const float wave = sinf(i * 1.7f) * 0.5f + 0.5f;
    const int h = 20 + static_cast<int>(wave * 100 * std::clamp((g_snapshot.left_dbfs + 40) / 40, 0.0f, 1.0f));
    M5.Display.fillRect(58 + i * 19, 475 - h, 12, h, i < 8 ? kGreen : kGrid);
  }
  text(g_snapshot.radio_text[0] ? g_snapshot.radio_text : "Waiting for RadioText",
       164, 555, TFT_WHITE, 2);

  const char* values[] = {
      g_snapshot.program_service[0] ? g_snapshot.program_service : "—",
      g_snapshot.radio_text[0] ? g_snapshot.radio_text : "—",
      g_snapshot.pi_code[0] ? g_snapshot.pi_code : "—",
      g_snapshot.program_type[0] ? g_snapshot.program_type : "—"};
  const int ys[] = {345, 415, 495, 565};
  M5.Display.fillRect(410, 315, 505, 285, kPanel);
  for (int i = 0; i < 4; ++i) text(values[i], 425, ys[i], TFT_WHITE, i == 1 ? 2 : 3, middle_left);

  M5.Display.fillRect(975, 330, 260, 260, kPanel);
  text(g_snapshot.stereo ? "Stereo" : "Mono", 1100, 348,
       g_snapshot.stereo ? kGreen : kMuted, 4);
  text(g_snapshot.rds_carrier ? "Present" : "Searching", 1100, 458,
       g_snapshot.rds_carrier ? kGreen : kMuted, 3);
  text(g_snapshot.rds_locked ? "Locked" : "Searching", 1100, 567,
       g_snapshot.rds_locked ? kGreen : kMuted, 3);
}

void health_card(int x, int y, int w, int h, const char* title, const char* value,
                 bool healthy) {
  card(x, y, w, h);
  text(title, x + w / 2, y + 24, kCyan, 2);
  text(value, x + w / 2, y + 70, healthy ? kGreen : kYellow, 3);
  M5.Display.drawCircle(x + w - 30, y + h - 28, 15, healthy ? kGreen : kYellow);
}

void draw_health_static() {
  card(24, 140, 445, 135);
  card(481, 140, 335, 135);
  card(828, 140, 428, 135);
  const int xs[] = {24, 326, 628, 930};
  for (int i = 0; i < 4; ++i) {
    card(xs[i], 290, 290, 112);
    card(xs[i], 414, 290, 112);
  }
  card(24, 538, 1232, 72);
}

void draw_health_dynamic() {
  char a[48], b[48], c[48];
  snprintf(a, sizeof(a), "%.1f MHz", g_snapshot.frequency_hz / 1000000.0);
  snprintf(b, sizeof(b), "%s", g_snapshot.running ? "RUNNING" : "STOPPED");
  snprintf(c, sizeof(c), "%s", g_snapshot.program_service[0] ? g_snapshot.program_service : "—");
  health_card(24, 140, 445, 135, "FREQUENCY", a, g_snapshot.running);
  health_card(481, 140, 335, 135, "STATUS", b, g_snapshot.running);
  health_card(828, 140, 428, 135, "STATION", c, g_snapshot.rds_locked);

  const int xs[] = {24, 326, 628, 930};
  char values[8][40];
  snprintf(values[0], sizeof(values[0]), "%.1f / %.1f kS/s",
           g_snapshot.effective_sps / 1000.0, g_snapshot.target_sps / 1000.0);
  snprintf(values[1], sizeof(values[1]), "%lu", static_cast<unsigned long>(g_snapshot.usb_overruns));
  snprintf(values[2], sizeof(values[2]), "%lu", static_cast<unsigned long>(g_snapshot.consumer_drops));
  snprintf(values[3], sizeof(values[3]), "%lu", static_cast<unsigned long>(g_snapshot.audio_underruns));
  snprintf(values[4], sizeof(values[4]), "%lu%%", static_cast<unsigned long>(g_snapshot.dsp_percent));
  if (g_snapshot.audio_ring_pressure_percent >= 0)
    snprintf(values[5], sizeof(values[5]), "%ld%%", static_cast<long>(g_snapshot.audio_ring_pressure_percent));
  else strlcpy(values[5], "N/A", sizeof(values[5]));
  strlcpy(values[6], g_snapshot.wifi_connected ? "Connected" : "Offline", sizeof(values[6]));
  strlcpy(values[7], g_snapshot.driver_ready ? "Ready" : "Waiting", sizeof(values[7]));
  const char* titles[] = {"EFFECTIVE SAMPLE RATE", "USB OVERRUNS", "CONSUMER DROPS",
                          "AUDIO UNDERRUNS", "DSP MAX TIME", "AUDIO RING PRESSURE",
                          "WI-FI STATE", "DRIVER STATE"};
  const bool good[] = {
      g_snapshot.effective_sps >= g_snapshot.target_sps * 95 / 100,
      g_snapshot.usb_overruns == 0, g_snapshot.consumer_drops == 0,
      g_snapshot.audio_underruns == 0, g_snapshot.dsp_percent < 80,
      g_snapshot.audio_ring_pressure_percent < 0 || g_snapshot.audio_ring_pressure_percent < 80,
      true, g_snapshot.driver_ready};
  for (int i = 0; i < 4; ++i) {
    health_card(xs[i], 290, 290, 112, titles[i], values[i], good[i]);
    health_card(xs[i], 414, 290, 112, titles[i + 4], values[i + 4], good[i + 4]);
  }
  M5.Display.fillRect(42, 552, 1190, 44, kPanel);
  text("LAST ERROR", 50, 574, kCyan, 2, middle_left);
  text(g_snapshot.last_error[0] ? g_snapshot.last_error : "—", 320, 574, TFT_WHITE, 2, middle_left);
  M5.Display.fillRect(300, 552, 800, 44, kPanel);
  const char* detail = g_snapshot.last_error[0] ? g_snapshot.last_error
                       : !g_snapshot.driver_ready ? "RTL-SDR waiting"
                       : g_snapshot.consumer_drops ? "IQ consumer drops recorded"
                       : g_snapshot.usb_overruns ? "USB overruns recorded"
                       : "No driver error";
  text(detail, 320, 574, TFT_WHITE, 2, middle_left);
  const bool overall = good[0] && good[1] && good[2] && good[3] && good[4] && good[7];
  text(overall ? "GOOD" : "CHECK", 1140, 574, overall ? kGreen : kYellow, 4, middle_right);
}

void draw_settings_static() {
  card(24, 145, 600, 460);
  label("FM AUDIO & TUNING", 48, 168);
  card(640, 145, 616, 460);
  label("FM OPERATIONS", 664, 168);
  button(48, 215, 250, 64, "SOUND", kGreen);
  button(320, 215, 132, 64, "VOL -", kCyan);
  button(468, 215, 132, 64, "VOL +", kCyan);
  button(48, 300, 250, 64, "STEP SIZE", kCyan);
  button(320, 300, 132, 64, "BW -", kCyan);
  button(468, 300, 132, 64, "BW +", kCyan);
  button(48, 385, 552, 64, "SPECTRUM GRAPHICS", kCyan);
  button(48, 470, 552, 64, "RECORDING", TFT_RED);
  button(664, 215, 568, 64, "SCAN / REBUILD PRESETS", kCyan);
  button(664, 300, 568, 64, "DEVICE SETTINGS", kCyan);
  button(664, 385, 568, 64, "HOME", kGreen, true);
  text("FM continues playing while these controls are used", 948, 505, kMuted, 2);
}

void draw_settings_dynamic() {
  char value[64];
  M5.Display.fillRect(65, 545, 530, 42, kPanel);
  snprintf(value, sizeof(value), "VOL %u   STEP %lu kHz   BW %lu kHz",
           g_snapshot.volume, static_cast<unsigned long>(g_snapshot.step_hz / 1000),
           static_cast<unsigned long>(g_snapshot.filter_bandwidth_hz / 1000));
  text(value, 65, 566, TFT_WHITE, 2, middle_left);
  M5.Display.fillRect(240, 230, 48, 34, kPanel);
  M5.Display.fillRect(530, 400, 54, 34, kPanel);
  M5.Display.fillRect(500, 485, 88, 34, kPanel);
  M5.Display.fillRect(1120, 230, 96, 34, kPanel);
  text(g_snapshot.sound_enabled ? "ON" : "OFF", 270, 247,
       g_snapshot.sound_enabled ? kGreen : TFT_RED, 2);
  text(g_snapshot.graphics_enabled ? "ON" : "OFF", 565, 417,
       g_snapshot.graphics_enabled ? kGreen : TFT_RED, 2, middle_right);
  text(g_snapshot.recording ? "ACTIVE" : "STANDBY", 565, 502,
       g_snapshot.recording ? TFT_RED : kMuted, 2, middle_right);
  text(g_snapshot.preset_scanning ? "SCANNING…" : "READY", 1200, 247,
       g_snapshot.preset_scanning ? kYellow : kGreen, 2, middle_right);
}

void draw_keypad() {
  M5.Display.fillRect(0, kHeaderH, 1280, kTabsY - kHeaderH, kBg);
  card(340, 150, 600, 450);
  label("ENTER FM FREQUENCY (MHz)", 375, 170);
  char field[24];
  snprintf(field, sizeof(field), "%s%s", g_entry, g_entry[0] ? " MHz" : "");
  M5.Display.fillRoundRect(380, 205, 520, 55, 8, TFT_NAVY);
  text(field[0] ? field : "88.0 – 108.0", 640, 233, TFT_WHITE, 3);
  static constexpr char keys[] = {'1','2','3','4','5','6','7','8','9','.','0','<'};
  for (int i = 0; i < 12; ++i) {
    char key[2] = {keys[i], 0};
    button(380 + (i % 3) * 174, 275 + (i / 3) * 60, 160, 50, key, kGrid);
  }
  button(380, 525, 250, 55, "CANCEL", TFT_RED);
  button(650, 525, 250, 55, "TUNE", kGreen, true);
}

void draw_view_static() {
  M5.Display.clearScrollRect();
  M5.Display.fillRect(0, kHeaderH, 1280, 720 - kHeaderH, kBg);
  if (g_keypad) {
    draw_keypad();
    return;
  }
  switch (g_view) {
    case View::listen: draw_listen_static(); break;
    case View::spectrum: draw_spectrum_static(); break;
    case View::station_rds: draw_station_static(); break;
    case View::rf_health: draw_health_static(); break;
    case View::settings: draw_settings_static(); break;
    default: break;
  }
  draw_tabs();
}

void draw_dynamic() {
  if (g_keypad) return;
  switch (g_view) {
    case View::listen: draw_listen_dynamic(); break;
    case View::spectrum: draw_spectrum_dynamic(); break;
    case View::station_rds: draw_station_dynamic(); break;
    case View::rf_health: draw_health_dynamic(); break;
    case View::settings: draw_settings_dynamic(); break;
    default: break;
  }
}

uint16_t waterfall_color(float level) {
  level = std::clamp(level, 0.0f, 1.0f);
  const uint8_t r = level < 0.5f ? 0 : static_cast<uint8_t>((level - 0.5f) * 510);
  const uint8_t g = level < 0.25f ? 0 : static_cast<uint8_t>(std::min(255.0f, (level - 0.25f) * 510));
  const uint8_t b = level < 0.65f ? static_cast<uint8_t>((0.65f - level) * 390) : 0;
  return M5.Display.color565(r, g, b);
}

}  // namespace

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_view = View::listen;
  g_active = true;
  g_keypad = false;
  audio_header::reset(g_audio_control);
  g_entry[0] = '\0';
  draw();
}

void leave() {
  M5.Display.clearScrollRect();
  g_active = false;
}

void draw() {
  if (!g_active) return;
  M5.Display.fillScreen(kBg);
  draw_header();
  draw_view_static();
  draw_dynamic();
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  const bool header_changed = snapshot.battery_percent != g_snapshot.battery_percent ||
                              snapshot.volume != g_snapshot.volume ||
                              snapshot.sound_enabled != g_snapshot.sound_enabled;
  g_snapshot = snapshot;
  const uint32_t now = millis();
  if (header_changed || audio_header::service_timeout(g_audio_control, now))
    audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                       g_snapshot.battery_percent);
  if (now - g_last_dynamic_ms < 150) return;
  g_last_dynamic_ms = now;
  draw_dynamic();
}

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, float floor) {
  if (!spectrum_active() || levels == nullptr || visible_bins < 2) return;
  M5.Display.startWrite();
  M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + 1, kSpectrumW - 2,
                      kSpectrumH - 2, kBg);
  for (int i = 1; i < 4; ++i) {
    M5.Display.drawFastVLine(kSpectrumX + i * kSpectrumW / 4, kSpectrumY,
                             kSpectrumH, kGrid);
    M5.Display.drawFastHLine(kSpectrumX, kSpectrumY + i * kSpectrumH / 4,
                             kSpectrumW, kGrid);
  }
  int px = kSpectrumX;
  int py = kSpectrumY + kSpectrumH - 2;
  for (size_t i = 0; i < visible_bins; ++i) {
    const float normalized = std::clamp((levels[first_bin + i] - floor) / 48.0f, 0.0f, 1.0f);
    const int x = kSpectrumX + static_cast<int>(i * (kSpectrumW - 1) / (visible_bins - 1));
    const int y = kSpectrumY + kSpectrumH - 2 - static_cast<int>(normalized * (kSpectrumH - 4));
    if (i) M5.Display.drawLine(px, py, x, y, kGreen);
    px = x;
    py = y;
    const int x0 = static_cast<int>(i * kSpectrumW / visible_bins);
    const int x1 = static_cast<int>((i + 1) * kSpectrumW / visible_bins);
    const uint16_t color = waterfall_color(normalized);
    for (int p = x0; p < x1; ++p) g_waterfall_row[p] = color;
  }
  const int center = kSpectrumX + kSpectrumW / 2;
  const int half_filter = std::clamp(static_cast<int>(
      static_cast<uint64_t>(g_snapshot.filter_bandwidth_hz) * kSpectrumW /
      (2u * (g_snapshot.span_hz ? g_snapshot.span_hz : 1u))), 3, kSpectrumW / 2 - 2);
  M5.Display.drawFastVLine(center, kSpectrumY, kSpectrumH, kCyan);
  M5.Display.drawFastVLine(center - half_filter, kSpectrumY, kSpectrumH, kCyan);
  M5.Display.drawFastVLine(center + half_filter, kSpectrumY, kSpectrumH, kCyan);
  M5.Display.scroll(0, -1);
  M5.Display.pushImage(kSpectrumX, kWaterfallY + kWaterfallH - 2,
                       kSpectrumW, 1, g_waterfall_row);
  M5.Display.drawFastVLine(center, kWaterfallY, kWaterfallH, kCyan);
  M5.Display.endWrite();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  const auto audio_action = audio_header::handle_touch(g_audio_control, x, y, millis());
  if (audio_action != audio_header::Action::none) {
    if (audio_action == audio_header::Action::opened ||
        audio_action == audio_header::Action::closed) {
      audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                         g_snapshot.battery_percent);
      return {};
    }
    if (audio_action == audio_header::Action::volume_down)
      return {ActionKind::volume_down};
    if (audio_action == audio_header::Action::sound_toggle)
      return {ActionKind::sound_toggle};
    return {ActionKind::volume_up};
  }
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_device_settings};
  if (g_keypad) {
    if (hit(x, y, 380, 525, 250, 55)) {
      g_keypad = false;
      g_entry[0] = '\0';
      draw_view_static();
      draw_dynamic();
      return {};
    }
    if (hit(x, y, 650, 525, 250, 55)) {
      char* end = nullptr;
      const double mhz = strtod(g_entry, &end);
      if (end != g_entry && *end == '\0' && mhz >= 88.0 && mhz <= 108.0) {
        g_keypad = false;
        const uint32_t hz = static_cast<uint32_t>(llround(mhz * 1000000.0));
        g_entry[0] = '\0';
        draw_view_static();
        return {ActionKind::tune_hz, hz};
      }
      return {};
    }
    static constexpr char keys[] = {'1','2','3','4','5','6','7','8','9','.','0','\b'};
    for (int i = 0; i < 12; ++i) {
      if (!hit(x, y, 380 + (i % 3) * 174, 275 + (i / 3) * 60, 160, 50)) continue;
      const size_t n = strlen(g_entry);
      if (keys[i] == '\b') {
        if (n) g_entry[n - 1] = '\0';
      } else if (n + 1 < sizeof(g_entry) &&
                 (keys[i] != '.' || strchr(g_entry, '.') == nullptr)) {
        g_entry[n] = keys[i];
        g_entry[n + 1] = '\0';
      }
      draw_keypad();
      return {};
    }
    return {};
  }

  if (y >= kTabsY) {
    const uint8_t next = std::min<uint8_t>(x / kTabW, static_cast<uint8_t>(View::count) - 1);
    if (next != static_cast<uint8_t>(g_view)) {
      g_view = static_cast<View>(next);
      draw();
    }
    return {};
  }
  if (g_view == View::listen && y >= 485 && y < 605) {
    if (x < 244) return {ActionKind::seek_down};
    if (x < 474) return {ActionKind::step_down};
    if (x < 764) {
      g_keypad = true;
      g_entry[0] = '\0';
      draw_view_static();
      return {};
    }
    if (x < 994) return {ActionKind::step_up};
    return {ActionKind::seek_up};
  }
  if (g_view == View::listen && hit(x, y, 24, 160, 300, 140))
    return {ActionKind::save_preset};
  if (g_view == View::spectrum) {
    if (hit(x, y, 390, 565, 70, 42)) return {ActionKind::span_down};
    if (hit(x, y, 820, 565, 70, 42)) return {ActionKind::span_up};
    if (hit(x, y, kSpectrumX, kSpectrumY, kSpectrumW, kSpectrumH + kWaterfallH + 30)) {
      const int64_t offset = (static_cast<int64_t>(x - (kSpectrumX + kSpectrumW / 2)) *
                              g_snapshot.span_hz) / kSpectrumW;
      const int64_t selected = static_cast<int64_t>(g_snapshot.frequency_hz) + offset;
      return {ActionKind::tune_hz, static_cast<uint32_t>(std::clamp<int64_t>(selected, kFmMinHz, kFmMaxHz))};
    }
  }
  if (g_view == View::settings) {
    if (hit(x, y, 48, 215, 250, 64)) return {ActionKind::sound_toggle};
    if (hit(x, y, 320, 215, 132, 64)) return {ActionKind::volume_down};
    if (hit(x, y, 468, 215, 132, 64)) return {ActionKind::volume_up};
    if (hit(x, y, 48, 300, 250, 64)) return {ActionKind::step_cycle};
    if (hit(x, y, 320, 300, 132, 64)) return {ActionKind::filter_down};
    if (hit(x, y, 468, 300, 132, 64)) return {ActionKind::filter_up};
    if (hit(x, y, 48, 385, 552, 64)) return {ActionKind::graphics_toggle};
    if (hit(x, y, 48, 470, 552, 64)) return {ActionKind::recording_toggle};
    if (hit(x, y, 664, 215, 568, 64)) return {ActionKind::scan_presets};
    if (hit(x, y, 664, 300, 568, 64)) return {ActionKind::open_device_settings};
    if (hit(x, y, 664, 385, 568, 64)) return {ActionKind::exit_to_browse};
  }
  return {};
}

bool active() { return g_active; }
bool spectrum_active() { return g_active && !g_keypad && g_view == View::spectrum; }
View view() { return g_view; }

void show_documentation_view(View requested, const Snapshot& snapshot,
                             bool show_volume_tray,
                             bool show_frequency_keypad) {
  if (requested >= View::count) return;
  g_snapshot = snapshot;
  g_view = requested;
  g_active = true;
  g_keypad = show_frequency_keypad;
  g_entry[0] = '\0';
  audio_header::reset(g_audio_control);
  if (show_volume_tray) {
    g_audio_control.expanded = true;
    g_audio_control.hide_at_ms = UINT32_MAX;
  }
  draw();
}

bool self_check() {
  return static_cast<uint8_t>(View::count) == 5 && kFmMinHz < kFmMaxHz &&
         kSpectrumX + kSpectrumW <= 1280 && kTabsY < 720 &&
         audio_header::self_check();
}

}  // namespace orcsdr::fm

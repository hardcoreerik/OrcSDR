#include "focus_nav.hpp"
#include "fm_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "fm_config.hpp"
#include "freq_keypad.hpp"
#include "redraw_guard.hpp"
#include "orc_badge.hpp"
#include "scope_canvas.hpp"
#include "spectrum_resample.hpp"

#include <M5Unified.h>
#include <esp_attr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::fm {
namespace {

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
constexpr uint32_t kFmMinHz = fmconfig::kMinFrequencyHz;
constexpr uint32_t kFmMaxHz = fmconfig::kMaxFrequencyHz;
constexpr int kSpectrumX = 46;
constexpr int kSpectrumY = 246;
constexpr int kSpectrumW = 1188;
constexpr int kSpectrumH = 145;
constexpr int kWaterfallY = 420;
constexpr int kWaterfallH = 130;

struct GainLayout {
  int auto_x;
  int auto_y;
  int auto_w;
  int auto_h;
  int slider_x;
  int slider_y;
  int slider_w;
};

static_assert(static_cast<uint8_t>(View::count) == 5);

Snapshot g_snapshot{};
View g_view = View::listen;
bool g_active = false;
bool g_keypad = false;
audio_header::Control g_audio_control{};
char g_entry[12]{};
uint32_t g_last_dynamic_ms = 0;
EXT_RAM_BSS_ATTR uint16_t g_waterfall_row[kSpectrumW]{};  // PSRAM, as in the AM dashboard

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

// Word-wraps value into at most max_lines lines no wider than width, each
// centred on cx, clipped to that column. RDS radio text is up to 64 padded
// characters and changes while scrolling, so it must never draw one long
// line across the neighbouring panels. Returns the number of lines drawn.
int wrapped_centered(const char* value, int cx, int first_y, int width, int line_h,
                     int max_lines, uint16_t color, int size) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s", value ? value : "");
  size_t len = strlen(buf);
  while (len && buf[len - 1] == ' ') buf[--len] = '\0';  // RDS pads with spaces
  M5.Display.setTextSize(size);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color);
  M5.Display.setClipRect(cx - width / 2, first_y - line_h / 2, width, max_lines * line_h);
  const char* p = buf;
  int lines = 0;
  while (*p && lines < max_lines) {
    while (*p == ' ') ++p;
    char line[96];
    size_t n = 0, last_space = 0;
    while (p[n]) {
      memcpy(line, p, n + 1);
      line[n + 1] = '\0';
      if (M5.Display.textWidth(line) > width) break;
      if (p[n] == ' ') last_space = n;
      ++n;
    }
    size_t take = n;
    if (p[n] && last_space) take = last_space;  // break at a word when one fits
    if (take == 0) take = 1;
    const bool last_line = lines + 1 == max_lines;
    memcpy(line, p, take);
    line[take] = '\0';
    if (last_line && p[take] && take > 1) {       // more text than room: mark it
      line[take - 1] = '\0';
      while (M5.Display.textWidth(line) + M5.Display.textWidth("...") > width && strlen(line))
        line[strlen(line) - 1] = '\0';
      strlcat(line, "...", sizeof(line));
    }
    M5.Display.drawString(line, cx, first_y + lines * line_h);
    ++lines;
    p += take;
  }
  M5.Display.clearClipRect();
  return lines;
}

void button(int x, int y, int w, int h, const char* title, uint16_t color = kCyan,
            bool selected = false) {
  orcsdr::focus_nav::note(x, y, w, h);
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
  audio_header::draw_brand("FM BROADCAST");
  M5.Display.drawFastVLine(365, 25, 82, kCyan);
  draw_radio_icon(456, 70, kCyan);
  text("FM Broadcast", 530, 66, TFT_WHITE, 4, middle_left);
  M5.Display.drawFastVLine(865, 25, 82, kCyan);
  audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                     g_snapshot.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(g_snapshot.sound_enabled);
  audio_header::draw_visualizer_button(g_snapshot.running);
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

void draw_segment_meter(int x, int y, int w, float dbfs, int segments = 18, int h = 32,
                        float floor_db = -40.0f, float top_db = 0.0f) {
  const float normalized = std::clamp((dbfs - floor_db) / (top_db - floor_db), 0.0f, 1.0f);
  const int lit = static_cast<int>(normalized * segments);
  const int gap = 3;
  const int sw = (w - (segments - 1) * gap) / segments;
  for (int i = 0; i < segments; ++i) {
    const uint16_t color = i < lit ? (i >= segments - 4 ? kYellow : kGreen) : kGrid;
    M5.Display.fillRect(x + i * (sw + gap), y, sw, h, color);
  }
}

// Stereo VU: instant attack, smooth release and a peak-hold marker, redrawn on
// its own ~30 fps tick (only when a segment changes) so it follows the
// programme instead of blinking once per page refresh.
// Pre-AGC programme level (not true dBFS; the AGC scales it down afterwards).
// Measured on 96.1 FM: median about +11, 90th percentile about +15, peaks to +29,
// so -10..+20 puts typical music around 10 of 14 segments.
constexpr float kVuFloorDb = -10.0f;
constexpr float kVuTopDb = 20.0f;
constexpr float kVuReleaseDbPerS = 20.0f;
constexpr uint32_t kVuPeakHoldMs = 1000;
constexpr uint32_t kVuFrameMs = 33;
constexpr int kVuSegments = 14;
constexpr int kVuX = 1000, kVuW = 220, kVuLeftY = 255, kVuRightY = 355;

struct VuChannel {
  float level_db = -90.0f;
  float peak_db = -90.0f;
  uint32_t peak_ms = 0;
  int drawn_lit = -1;
  int drawn_peak = -2;
};
VuChannel g_vu[2];
uint32_t g_vu_ms = 0;

int vu_segments_lit(float db) {
  const float norm = std::clamp((db - kVuFloorDb) / (kVuTopDb - kVuFloorDb), 0.0f, 1.0f);
  return static_cast<int>(lroundf(norm * kVuSegments));
}

void vu_step(VuChannel& c, float input_db, uint32_t now, float dt_s) {
  const float fall = kVuReleaseDbPerS * dt_s;
  c.level_db = input_db >= c.level_db ? input_db : std::max(input_db, c.level_db - fall);
  if (c.level_db >= c.peak_db) {
    c.peak_db = c.level_db;
    c.peak_ms = now;
  } else if (now - c.peak_ms > kVuPeakHoldMs) {
    c.peak_db = std::max(c.level_db, c.peak_db - fall);
  }
}

uint16_t vu_zone_color(int segment) {
  if (segment >= kVuSegments - 2) return TFT_RED;
  if (segment >= kVuSegments - 5) return kYellow;
  return kGreen;
}

void draw_vu_channel(VuChannel& c, int y, bool force) {
  const int lit = vu_segments_lit(c.level_db);
  const int peak = vu_segments_lit(c.peak_db) - 1;  // hold marker segment, -1 = none
  if (!force && lit == c.drawn_lit && peak == c.drawn_peak) return;
  constexpr int gap = 3;
  constexpr int sw = (kVuW - (kVuSegments - 1) * gap) / kVuSegments;
  for (int i = 0; i < kVuSegments; ++i) {
    // The peak-hold marker takes its zone colour (green / yellow / red).
    const uint16_t color = i < lit || i == peak ? vu_zone_color(i) : kGrid;
    M5.Display.fillRect(kVuX + i * (sw + gap), y, sw, 32, color);
  }
  c.drawn_lit = lit;
  c.drawn_peak = peak;
}

void service_vu(uint32_t now, bool force) {
  const uint32_t elapsed = now - g_vu_ms;
  if (!force && elapsed < kVuFrameMs) return;
  const float dt_s = std::min(0.2f, elapsed / 1000.0f);
  g_vu_ms = now;
  vu_step(g_vu[0], g_snapshot.left_dbfs, now, dt_s);
  vu_step(g_vu[1], g_snapshot.right_dbfs, now, dt_s);
  M5.Display.startWrite();
  draw_vu_channel(g_vu[0], kVuLeftY, force);
  draw_vu_channel(g_vu[1], kVuRightY, force);
  M5.Display.endWrite();
}

int relative_percent() {
  return std::clamp(static_cast<int>(lroundf((g_snapshot.relative_dbfs + 90.0f) *
                                             (100.0f / 90.0f))), 0, 100);
}

GainLayout gain_layout() {
  if (g_view == View::listen) return {44, 405, 80, 42, 145, 420, 145};
  if (g_view == View::spectrum) return {928, 561, 82, 44, 1024, 584, 190};
  return {48, 542, 190, 58, 280, 568, 900};
}

void draw_gain_control(bool compact) {
  const GainLayout layout = gain_layout();
  char value[24];
  if (g_snapshot.clipping_percent > 0.1f)
    snprintf(value, sizeof(value), "CLIP %.2f%%",
             static_cast<double>(g_snapshot.clipping_percent));
  else if (g_snapshot.gain_auto)
    snprintf(value, sizeof(value), g_snapshot.gain_auto_selecting ? "SMART..." : "SMART %.1f",
             static_cast<double>(g_snapshot.gain_tenth_db) / 10.0);
  else
    snprintf(value, sizeof(value), "%.1f dB",
             static_cast<double>(g_snapshot.gain_tenth_db) / 10.0);
  // SMART <-> MANUAL mode toggle; in MANUAL the slider sets the tuner gain.
  button(layout.auto_x, layout.auto_y, layout.auto_w, layout.auto_h,
         g_snapshot.gain_auto ? "SMART" : "MANUAL",
         g_snapshot.gain_auto ? kGreen : TFT_ORANGE, true);
  text(compact ? "GAIN" : "RF GAIN", layout.slider_x,
       layout.slider_y - (compact ? 14 : 28), kCyan, 2, middle_left);
  text(value, layout.slider_x + layout.slider_w,
       layout.slider_y - (compact ? 14 : 28),
       g_snapshot.clipping_percent > 0.1f
           ? TFT_RED : g_snapshot.gain_auto ? kGreen : TFT_WHITE,
       2, middle_right);
  M5.Display.fillRoundRect(layout.slider_x, layout.slider_y, layout.slider_w, 18, 9, kGrid);
  const int gain_x = layout.slider_x + std::clamp(g_snapshot.gain_tenth_db, 0, 496) *
                                         layout.slider_w / 496;
  if (!g_snapshot.gain_auto)
    M5.Display.fillRoundRect(layout.slider_x, layout.slider_y,
                             std::max(9, gain_x - layout.slider_x), 18, 9, kGreen);
  M5.Display.fillCircle(gain_x, layout.slider_y + 9, compact ? 11 : 14,
                        g_snapshot.gain_auto ? kMuted : kGreen);
}

// Listen page widgets repaint only when their inputs change (see redraw_guard.hpp).
struct ListenGuards {
  ui::RedrawGuard preset, level, percent, gain, frequency, station, radio_text, running,
      stereo;
  void invalidate_all() {
    for (ui::RedrawGuard* g : {&preset, &level, &percent, &gain, &frequency, &station,
                               &radio_text, &running, &stereo})
      g->invalidate();
  }
} g_listen;

void draw_listen_static() {
  g_listen.invalidate_all();
  card(24, 160, 300, 140);
  label("PRESET", 44, 177);
  card(24, 316, 300, 145);
  label("RELATIVE LEVEL", 44, 333);
  card(942, 160, 314, 301);
  label("STEREO VU", 962, 177);
  text("L", 964, 270, TFT_WHITE, 3, middle_left);
  text("R", 964, 370, TFT_WHITE, 3, middle_left);
  g_vu[0].drawn_lit = g_vu[1].drawn_lit = -1;  // repaint all segments next tick
  const int widths[] = {220, 220, 280, 220, 220};
  const char* names[] = {"<<  SEEK -", "<  STEP -", "ENTER FREQUENCY", "STEP +  >", "SEEK +  >>"};
  int x = 24;
  for (int i = 0; i < 5; ++i) {
    button(x, 485, widths[i], 120, names[i], i == 2 ? kCyan : kGrid, i == 2);
    x += widths[i] + 10;
  }
}

void draw_listen_dynamic() {
  using ui::Signature;
  char value[32];
  if (g_listen.preset.changed(Signature().add(g_snapshot.preset_index)
                                  .add(g_snapshot.preset_count != 0))) {
    M5.Display.fillRect(42, 211, 264, 73, kPanel);
    snprintf(value, sizeof(value), "%02u", g_snapshot.preset_index);
    text(value, 45, 248, TFT_WHITE, 5, middle_left);
    text(g_snapshot.preset_count ? "*" : "+", 270, 248, kGreen, 5);
  }

  // Level meter: every segment is repainted solid, so no clear is needed. It is
  // 24 px tall so the gain row below can repaint on its own.
  const float level_norm = std::clamp((g_snapshot.relative_dbfs + 40.0f) / 40.0f, 0.0f, 1.0f);
  if (g_listen.level.changed(Signature().add(static_cast<int>(level_norm * 10))))
    draw_segment_meter(45, 371, 245, g_snapshot.relative_dbfs, 10, 24);
  const int percent = relative_percent();
  if (g_listen.percent.changed(Signature().add(percent))) {
    M5.Display.fillRect(226, 334, 80, 18, kPanel);
    snprintf(value, sizeof(value), "%d%%", percent);
    text(value, 290, 343, kGreen, 2, middle_right);
  }
  const bool clipping = g_snapshot.clipping_percent > 0.1f;
  if (g_listen.gain.changed(
          Signature()
              .add(g_snapshot.gain_auto)
              .add(g_snapshot.gain_auto_selecting)
              .add(g_snapshot.gain_tenth_db)
              .add(clipping)
              .add(clipping ? static_cast<int>(g_snapshot.clipping_percent * 100.0f) : 0))) {
    M5.Display.fillRect(40, 397, 272, 54, kPanel);
    draw_gain_control(true);
  }

  // Centre column: each item clears only its own band.
  if (g_listen.frequency.changed(Signature().add(g_snapshot.frequency_hz))) {
    M5.Display.fillRect(340, 196, 580, 78, kBg);
    snprintf(value, sizeof(value), "%.1f", g_snapshot.frequency_hz / 1000000.0);
    text(value, 615, 235, TFT_WHITE, 8);
    text("MHz", 850, 252, TFT_WHITE, 4);
  }
  const char* ps = g_snapshot.program_service[0] ? g_snapshot.program_service : "—";
  if (g_listen.station.changed(Signature().add(ps))) {
    M5.Display.fillRect(340, 316, 580, 40, kBg);
    text(ps, 630, 335, TFT_WHITE, 4);
  }
  // Radio text: centred under the station name, wrapped inside the centre
  // column (x 350-910) so it stays clear of RELATIVE LEVEL and STEREO VU.
  const char* rt = g_snapshot.radio_text[0] ? g_snapshot.radio_text : "RDS text unavailable";
  if (g_listen.radio_text.changed(Signature().add(rt))) {
    M5.Display.fillRect(340, 360, 580, 48, kBg);
    (void)wrapped_centered(rt, 630, 372, 560, 22, 2, TFT_WHITE, 2);
  }
  if (g_listen.running.changed(Signature().add(g_snapshot.running)))
    button(455, 415, 175, 44, g_snapshot.running ? "RUNNING" : "STOPPED",
           g_snapshot.running ? kGreen : TFT_RED, true);
  if (g_listen.stereo.changed(Signature().add(g_snapshot.stereo)))
    button(680, 415, 160, 44, g_snapshot.stereo ? "STEREO" : "MONO",
           g_snapshot.stereo ? kGreen : kMuted);

  service_vu(millis(), false);
}

void draw_spectrum_static() {
  card(24, 140, 1232, 470);
  label("CENTER FREQUENCY", 46, 158);
  label("DSP FILTER BW", 548, 158);
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
  text("TAP SPECTRUM TO TUNE", 650, 585, TFT_WHITE, 2);
}

// Status pills for the tuned station: lit when the condition holds, dim otherwise.
void status_badge(int x, int y, int w, int h, const char* label_text, bool on, uint16_t color) {
  M5.Display.fillRoundRect(x, y, w, h, 8, on ? color : kGrid);
  M5.Display.drawRoundRect(x, y, w, h, 8, on ? color : kMuted);
  text(label_text, x + w / 2, y + h / 2, on ? TFT_BLACK : kMuted, 2, middle_center);
}

// The IQ level as a filled pill: green fill that grows with the level, red and labelled CLIP when the IQ
// is overloading.
void iq_badge(int x, int y, int w, int h, float dbfs, bool clipping) {
  const uint16_t color = clipping ? TFT_RED : kGreen;
  const float level = std::clamp((dbfs + 40.0f) / 40.0f, 0.0f, 1.0f);
  const int fill = clipping ? w - 2 : static_cast<int>((w - 2) * level);
  M5.Display.fillRoundRect(x, y, w, h, 8, kGrid);
  if (fill > 0) M5.Display.fillRoundRect(x + 1, y + 1, fill, h - 2, 7, color);
  M5.Display.drawRoundRect(x, y, w, h, 8, clipping ? TFT_RED : kMuted);
  text(clipping ? "CLIP" : "IQ", x + w / 2, y + h / 2, TFT_WHITE, 2, middle_center);
}

// Status badges and separate left and right audio meters, each on its own row, large enough to read at a
// glance and kept entirely above the spectrum plot (which starts at y 246). STEREO and RDS light when
// locked; the IQ pill shows the IQ level and turns red when the IQ is overloading.
// The meters follow the sampled level up at once and fall back at 20 dB a second (like the listen page's VU),
// timed from the clock rather than counted per paint because paints are only a minimum of 150 ms apart.
// The audio levels are pre-AGC programme levels (typically +11 to +15, peaks to +29), so they use the VU
// range, not the -40..0 dBFS range of the IQ level. This sees one sampled value per paint, so a peak that
// falls between two samples can still be missed.
constexpr float kMeterReleaseDbPerS = 20.0f;

float meter_release(float shown_db, float level_db, float elapsed_s) {
  return std::max(level_db, shown_db - kMeterReleaseDbPerS * elapsed_s);
}

void draw_signal_panel() {
  static float shown_iq_db = -90.0f, shown_left_db = -90.0f, shown_right_db = -90.0f;
  static uint32_t last_paint_ms = 0, shown_frequency_hz = 0;
  const uint32_t now = millis();
  // A new station, or coming back after more than a second away from this view, must not inherit the old
  // bars: the release below is capped per paint, so a stale high level would linger.
  const bool stale = last_paint_ms != 0 && now - last_paint_ms > 1000u;
  if (shown_frequency_hz != g_snapshot.frequency_hz || stale) {
    shown_frequency_hz = g_snapshot.frequency_hz;
    shown_iq_db = shown_left_db = shown_right_db = -90.0f;
  }
  const float elapsed_s = last_paint_ms == 0 ? 0.0f : std::min(1.0f, (now - last_paint_ms) / 1000.0f);
  last_paint_ms = now;
  shown_iq_db = meter_release(shown_iq_db, g_snapshot.relative_dbfs, elapsed_s);
  shown_left_db = meter_release(shown_left_db, g_snapshot.left_dbfs, elapsed_s);
  shown_right_db = meter_release(shown_right_db, g_snapshot.right_dbfs, elapsed_s);
  constexpr int kPanelX = 908, kPanelY = 146, kPanelW = 332, kPanelH = 96;
  M5.Display.fillRect(kPanelX, kPanelY, kPanelW, kPanelH, kPanel);
  const bool clipping = g_snapshot.clipping_percent > 0.1f;
  status_badge(912, 148, 104, 30, g_snapshot.stereo ? "STEREO" : "MONO", g_snapshot.stereo, kGreen);
  status_badge(1024, 148, 104, 30, g_snapshot.rds_locked ? "RDS" : g_snapshot.rds_carrier ? "RDS..." : "NO RDS",
               g_snapshot.rds_locked, kCyan);
  iq_badge(1136, 148, 100, 30, shown_iq_db, clipping);
  text("L", 920, 196, TFT_WHITE, 2, middle_left);
  draw_segment_meter(944, 184, 292, shown_left_db, 22, 24, kVuFloorDb, kVuTopDb);
  text("R", 920, 226, TFT_WHITE, 2, middle_left);
  draw_segment_meter(944, 214, 292, shown_right_db, 22, 24, kVuFloorDb, kVuTopDb);
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
  draw_signal_panel();
  snprintf(value, sizeof(value), "%.1f MHz", g_snapshot.span_hz / 1000000.0);
  M5.Display.fillRect(125, 565, 210, 42, kPanel);
  text(value, 220, 585, TFT_WHITE, 3);
  draw_gain_control(true);
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
}

void draw_settings_dynamic() {
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
  draw_gain_control(false);
}

void draw_keypad() {
  freq_keypad::draw(kHeaderH, kBg, "ENTER FM FREQUENCY", "76.0 - 108.0", "MHz", g_entry);
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
  if (g_view == View::listen && !g_keypad) service_vu(now, false);
  if (now - g_last_dynamic_ms < 150) return;
  g_last_dynamic_ms = now;
  draw_dynamic();
}

scope::FrameStats g_scope_stats;
scope::Trace g_scope_trace;
// Slow-falling peak-hold trace behind the live one: shows where signals have been, not just now. It is kept
// in dB, not as a fraction of the screen: the display floor follows the loudest bin every frame, so a stored
// fraction would drift to the wrong height whenever that bin changed. Reset when the tuned frequency or span
// changes, because the bins no longer mean the same thing.
constexpr float kPeakHoldFallDbPerFrame = 0.25f;   // about 5 dB a second at 20 frames a second
constexpr float kPeakHoldEmptyDb = -200.0f;
EXT_RAM_BSS_ATTR float g_peak_hold_db[kSpectrumW]{};
uint32_t g_peak_frequency_hz = 0, g_peak_span_hz = 0, g_last_spectrum_frame_ms = 0;

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, float floor) {
  if (!spectrum_active() || levels == nullptr || visible_bins < 2) return;
  const uint32_t frame_started_ms = millis();
  // The trace is drawn off-screen and pushed in one go. Erasing and redrawing the screen area every
  // frame made the trace and its lines flicker and cost time on every frame.
  constexpr int w = kSpectrumW - 2;
  constexpr int h = kSpectrumH - 2;
  M5Canvas* canvas = g_scope_trace.begin(w, h, kBg);
  const int trace_x0 = kSpectrumX + 1;
  if (canvas != nullptr) {
    for (int i = 1; i < 4; ++i) {
      canvas->drawFastVLine(i * kSpectrumW / 4 - 1, 0, h, kGrid);
      canvas->drawFastHLine(0, i * kSpectrumH / 4 - 1, w, kGrid);
    }
  }
  // The held peaks only fall while this view is drawing, so after time away they are stale: start over.
  const bool resumed = g_last_spectrum_frame_ms != 0 && frame_started_ms - g_last_spectrum_frame_ms > 1000u;
  g_last_spectrum_frame_ms = frame_started_ms;
  if (resumed || g_peak_frequency_hz != g_snapshot.frequency_hz || g_peak_span_hz != g_snapshot.span_hz) {
    g_peak_frequency_hz = g_snapshot.frequency_hz;
    g_peak_span_hz = g_snapshot.span_hz;
    for (size_t i = 0; i < kSpectrumW; ++i) g_peak_hold_db[i] = kPeakHoldEmptyDb;
  }
  int px = 0;
  int py = h - 3;
  for (size_t i = 0; i < kSpectrumW; ++i) {
    const float level = spectrum::peak_for_pixel(
        levels, first_bin, visible_bins, i, kSpectrumW);
    const float normalized = std::clamp((level - floor) / 48.0f, 0.0f, 1.0f);
    const int x = static_cast<int>(i) - 1;
    const int y = h - 3 - static_cast<int>(normalized * (kSpectrumH - 4));
    g_peak_hold_db[i] = std::max(level, g_peak_hold_db[i] - kPeakHoldFallDbPerFrame);
    if (canvas != nullptr && x >= 0 && x < w) {
      // Peak hold first so the live trace draws over it, scaled with this frame's floor.
      const float held = std::clamp((g_peak_hold_db[i] - floor) / 48.0f, 0.0f, 1.0f);
      canvas->drawPixel(x, h - 3 - static_cast<int>(held * (kSpectrumH - 4)), 0xFD20);
    }
    if (canvas != nullptr && i && x >= 0 && x < w) {
      canvas->drawLine(px, py, x, y, kGreen);
    }
    px = x;
    py = y;
    g_waterfall_row[i] = waterfall_color(normalized);
  }
  if (canvas != nullptr) {
    const int center = kSpectrumW / 2 - 1;
    const int half_filter = std::clamp(static_cast<int>(
        static_cast<uint64_t>(g_snapshot.filter_bandwidth_hz) * kSpectrumW /
        (2u * (g_snapshot.span_hz ? g_snapshot.span_hz : 1u))), 3, kSpectrumW / 2 - 2);
    canvas->drawFastVLine(center, 0, h, kCyan);
    canvas->drawFastVLine(center - half_filter, 0, h, kCyan);
    canvas->drawFastVLine(center + half_filter, 0, h, kCyan);
    canvas->pushSprite(trace_x0, kSpectrumY + 1);
  }
  scope::scroll_waterfall(kSpectrumX, kWaterfallY + kWaterfallH - 2, kSpectrumW, g_waterfall_row);
  g_scope_stats.frame_done(frame_started_ms);
}

uint32_t spectrum_fps() { return g_scope_stats.fps(); }
uint32_t spectrum_draw_ms() { return g_scope_stats.draw_ms(); }

Action gain_mode_toggle();

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_device_settings};
  if (g_keypad) {
    const auto result = freq_keypad::handle_touch(x, y, g_entry, sizeof(g_entry));
    if (result == freq_keypad::Result::cancelled) {
      g_keypad = false;
      g_entry[0] = '\0';
      draw_view_static();
      draw_dynamic();
      return {};
    }
    if (result == freq_keypad::Result::submitted) {
      char* end = nullptr;
      const double mhz = strtod(g_entry, &end);
      if (end != g_entry && *end == '\0' &&
          mhz >= kFmMinHz / 1000000.0 && mhz <= kFmMaxHz / 1000000.0) {
        g_keypad = false;
        const uint32_t hz = static_cast<uint32_t>(llround(mhz * 1000000.0));
        g_entry[0] = '\0';
        draw_view_static();
        return {ActionKind::tune_hz, hz};
      }
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
  if (g_view == View::listen || g_view == View::spectrum || g_view == View::settings) {
    const GainLayout layout = gain_layout();
    if (hit(x, y, layout.auto_x, layout.auto_y, layout.auto_w, layout.auto_h))
      return gain_mode_toggle();
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

// SMART -> MANUAL holds the gain SMART chose, so the level does not jump;
// MANUAL -> SMART hands control back to the automatic search.
Action gain_mode_toggle() {
  if (g_snapshot.gain_auto)
    return {ActionKind::gain_tenth_db,
            static_cast<uint32_t>(std::max(0, static_cast<int>(g_snapshot.gain_tenth_db)))};
  return {ActionKind::gain_auto};
}

Action handle_gain_drag(int32_t x, int32_t y) {
  const GainLayout layout = gain_layout();
  if (!g_active || (g_view != View::listen && g_view != View::spectrum &&
                    g_view != View::settings) ||
      !hit(x, y, layout.slider_x - 14, layout.slider_y - 24,
           layout.slider_w + 28, 66) || g_snapshot.gain_step_count == 0)
    return {};
  const int raw_index = static_cast<int>(x - layout.slider_x) *
                        static_cast<int>(g_snapshot.gain_step_count) / layout.slider_w;
  const size_t index = static_cast<size_t>(std::clamp(
      raw_index, 0, static_cast<int>(g_snapshot.gain_step_count) - 1));
  const int gain = g_snapshot.gain_steps_tenth_db[index];
  if (!g_snapshot.gain_auto && gain == g_snapshot.gain_tenth_db) return {};
  return {ActionKind::gain_tenth_db, static_cast<uint32_t>(gain)};
}

bool active() { return g_active; }
bool keypad_open() { return g_active && g_keypad; }

void begin_frequency_entry() {
  if (!g_active || g_keypad || g_view != View::listen) return;
  g_keypad = true;
  g_entry[0] = '\0';
  draw_view_static();
}

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
         gain_layout().slider_x + gain_layout().slider_w <= 1280 &&
         audio_header::self_check();
}

}  // namespace orcsdr::fm

#include "weather_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "focus_nav.hpp"
#include "offline_map.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::weather {
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
static_assert(static_cast<uint8_t>(Tab::count) == 5);

Snapshot g_snapshot{};
Tab g_tab = Tab::now;
bool g_active = false;
audio_header::Control g_audio{};

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}
void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          int size = 2, textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color, kBg);
  M5.Display.drawString(value ? value : "", x, y);
}
void card(int x, int y, int w, int h, const char* title) {
  M5.Display.fillRoundRect(x, y, w, h, 12, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 12, kCyan);
  text(title, x + 18, y + 14, kCyan, 2, top_left);
}
void button(int x, int y, int w, int h, const char* label, uint16_t color = kCyan) {
  focus_nav::note(x, y, w, h);
  M5.Display.fillRoundRect(x, y, w, h, 9, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 9, color);
  text(label, x + w / 2, y + h / 2, color == kMuted ? kMuted : TFT_WHITE, 2);
}
const char* policy_name(OnlinePolicy p) {
  switch (p) {
    case OnlinePolicy::disabled: return "NET DISABLED";
    case OnlinePolicy::manual: return "NET MANUAL";
    case OnlinePolicy::automatic: return "NET AUTO";
  }
  return "NET DISABLED";
}
void draw_header() {
  M5.Display.fillRect(0, 0, 1280, kHeaderH, kBg);
  M5.Display.drawFastHLine(8, kHeaderH - 1, 1264, kCyan);
  audio_header::draw_brand("WEATHER");
  text(g_snapshot.location_configured ? g_snapshot.location_label : "LOCATION NOT SET",
       440, 38, g_snapshot.location_configured ? TFT_WHITE : kYellow, 2, middle_left);
  text(g_snapshot.rtc_valid ? g_snapshot.local_time : "TIME RELATIVE",
       440, 72, g_snapshot.rtc_valid ? kGreen : kMuted, 2, middle_left);
  text(policy_name(g_snapshot.online_policy), 700, 72,
       g_snapshot.online_policy == OnlinePolicy::disabled ? kGreen : kYellow, 2, middle_left);
  audio_header::draw(g_audio, g_snapshot.volume, g_snapshot.sound_enabled,
                     g_snapshot.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(g_snapshot.sound_enabled);
  audio_header::draw_visualizer_button(g_snapshot.rf_running);
  audio_header::draw_settings_button();
}
void draw_tabs() {
  static constexpr const char* names[] = {"NOW","FORECAST","MAP","RF WEATHER","REPORTS"};
  for (uint8_t i = 0; i < static_cast<uint8_t>(Tab::count); ++i) {
    const int x = i * kTabW;
    const bool selected = i == static_cast<uint8_t>(g_tab);
    M5.Display.fillRect(x, kTabsY, kTabW, 90, selected ? 0x0a43 : kPanel);
    M5.Display.drawRect(x, kTabsY, kTabW, 90, selected ? kCyan : kGrid);
    text(names[i], x + kTabW / 2, kTabsY + 45,
         selected ? kCyan : TFT_WHITE, 2);
  }
}
void source_badge(const char* source, uint32_t age_s, int x, int y) {
  char line[48];
  char age[20];
  format_age(age, sizeof(age), age_s);
  std::snprintf(line, sizeof(line), "%s  %s", source, age);
  text(line, x, y, kMuted, 1, middle_left);
}
void draw_now() {
  card(24, 154, 380, 202, "LOCAL OBSERVATIONS");
  text("No current surface sensor", 48, 220, kMuted, 2, middle_left);
  text("measurement available", 48, 252, kMuted, 2, middle_left);
  text("Use RF WEATHER to collect direct RF.", 48, 307, TFT_WHITE, 1, middle_left);

  card(424, 154, 404, 202, "WEATHER RADIO");
  char freq[32];
  std::snprintf(freq, sizeof(freq), "%.3f MHz", g_snapshot.current_noaa_hz / 1000000.0);
  text(freq, 626, 222, TFT_WHITE, 4);
  char db[32];
  std::snprintf(db, sizeof(db), g_snapshot.rf_running ? "%.1f dBFS REL" : "NOT LISTENING",
                static_cast<double>(g_snapshot.relative_dbfs));
  text(db, 626, 274, g_snapshot.rf_running ? kGreen : kMuted, 2);
  source_badge("RF", g_snapshot.rf_age_seconds, 450, 328);

  card(848, 154, 408, 202, "ALERTS");
  text(g_snapshot.noaa_catalog_installed ? "NOAA catalog: INSTALLED"
                                          : "NOAA catalog: NOT INSTALLED",
       872, 212, g_snapshot.noaa_catalog_installed ? kGreen : kMuted, 1, middle_left);
  if (g_snapshot.noaa_catalog_date[0])
    text(g_snapshot.noaa_catalog_date, 872, 238, kMuted, 1, middle_left);
  text("SAME decode is not in Foundation.", 872, 266, kMuted, 1, middle_left);
  text("No alert claimed unless decoded.", 872, 252, kGreen, 1, middle_left);
  text("Network alerts are not required.", 872, 290, kMuted, 1, middle_left);

  card(24, 376, 1232, 226, "FIELD STATUS");
  text(g_snapshot.status[0] ? g_snapshot.status : "Offline-ready. RF collection is on demand.",
       50, 430, TFT_WHITE, 2, middle_left);
  text(g_snapshot.sd_ready ? "SD REPORTS READY" : "SD UNAVAILABLE",
       50, 482, g_snapshot.sd_ready ? kGreen : kYellow, 2, middle_left);
  text(g_snapshot.map_ready ? "ORCMAPS READY" : "ORCMAPS PACK NOT LOADED",
       390, 482, g_snapshot.map_ready ? kGreen : kMuted, 2, middle_left);
  text(g_snapshot.rtl_ready ? "RTL-SDR READY" : "RTL-SDR UNAVAILABLE",
       810, 482, g_snapshot.rtl_ready ? kGreen : kYellow, 2, middle_left);
}
void draw_forecast() {
  card(24, 154, 1232, 448, "OFFLINE FORECAST / OUTLOOK");
  text("OrcSDR does not invent a forecast from sparse RF observations.",
       48, 220, TFT_WHITE, 2, middle_left);
  text("Cached or optional online forecast providers can populate this page later.",
       48, 262, kMuted, 2, middle_left);
  text("Default network policy:", 48, 332, kCyan, 2, middle_left);
  text(policy_name(g_snapshot.online_policy), 330, 332,
       g_snapshot.online_policy == OnlinePolicy::disabled ? kGreen : kYellow, 2, middle_left);
  button(48, 390, 280, 64, "CYCLE NET POLICY",
         g_snapshot.online_policy == OnlinePolicy::disabled ? kGreen : kYellow);
  text("Internet is enrichment only; Weather works without it.",
       48, 500, kGreen, 2, middle_left);
}
void draw_map() {
  card(24, 154, 1232, 448, "ORCMAPS WEATHER CONTEXT");
  if (!g_snapshot.location_configured) {
    text("Set receiver location to enable local weather context.", 640, 290, kYellow, 2);
  } else if (!g_snapshot.map_ready) {
    text("Offline map pack not loaded.", 640, 270, kMuted, 2);
    text("Receiver location remains available to RF/report features.", 640, 316, TFT_WHITE, 2);
  } else {
    const offline_map::View view{
        static_cast<float>(g_snapshot.latitude_e7) / 1.0e7f,
        static_cast<float>(g_snapshot.longitude_e7) / 1.0e7f,
        25.0f, 48, 202, 1184, 326};
    offline_map::draw_base(M5.Display, view, 0x0186, kGrid, kMuted, kCyan);
    int mx = 0, my = 0;
    if (offline_map::project(view, view.center_lat, view.center_lon, &mx, &my)) {
      M5.Display.fillCircle(mx, my, 7, kGreen);
      M5.Display.drawCircle(mx, my, 10, TFT_WHITE);
    }
    char coords[64];
    std::snprintf(coords, sizeof(coords), "YOU  %.5f, %.5f",
                  g_snapshot.latitude_e7 / 1e7, g_snapshot.longitude_e7 / 1e7);
    text(coords, 54, 550, TFT_WHITE, 1, middle_left);
    text("OFFLINE ORCMAPS • Foundation has no live radar layer",
         1224, 550, kMuted, 1, middle_right);
  }
}
void draw_rf() {
  card(24, 154, 394, 448, "NOAA WEATHER RADIO");
  char freq[32];
  std::snprintf(freq, sizeof(freq), "%.3f MHz", g_snapshot.current_noaa_hz / 1000000.0);
  text(freq, 221, 220, TFT_WHITE, 4);
  button(48, 280, 160, 64, g_snapshot.rf_running ? "LISTENING" : "LISTEN",
         g_snapshot.rf_running ? kGreen : kCyan);
  button(230, 280, 160, 64, "SCAN 7 CH");
  button(48, 362, 342, 58, "STOP RF", g_snapshot.rf_running ? kYellow : kMuted);
  if (g_snapshot.noaa_scan.complete) {
    char best[64];
    std::snprintf(best, sizeof(best), "BEST %.3f MHz  %.1f dBFS",
                  g_snapshot.noaa_scan.strongest_frequency_hz / 1000000.0,
                  static_cast<double>(g_snapshot.noaa_scan.strongest_dbfs));
    text(best, 48, 456, kGreen, 2, middle_left);
  } else {
    text("Scan is sequential; one tuner, one channel.", 48, 456, kMuted, 1, middle_left);
  }
  text(g_snapshot.noaa_catalog_installed
           ? "Signed NOAA catalog installed • voice RX only in Foundation"
           : "Install NOAA WEATHER in Data & Maps for local reference data",
       48, 518, kMuted, 1, middle_left);

  card(438, 154, 394, 448, "LOCAL RF WEATHER");
  text("Weather Hunter", 462, 220, kMuted, 2, middle_left);
  text("Personal sensors", 462, 270, kMuted, 2, middle_left);
  text("Radiosondes / Balloon Hunter", 462, 320, kMuted, 2, middle_left);
  text("Phase 2 — not claimed as implemented.", 462, 390, kYellow, 1, middle_left);

  card(852, 154, 404, 448, "SATELLITE WEATHER");
  text("Pass prediction / LRPT receive", 876, 220, kMuted, 2, middle_left);
  text("Phase 3 — not claimed as implemented.", 876, 270, kYellow, 1, middle_left);
  text("No blind background 137 MHz polling.", 876, 330, kGreen, 1, middle_left);
}
void draw_reports() {
  card(24, 154, 1232, 448, "WEATHER REPORTS / HISTORY");
  char count[48];
  std::snprintf(count, sizeof(count), "%u report%s",
                static_cast<unsigned>(g_snapshot.report_count),
                g_snapshot.report_count == 1 ? "" : "s");
  text(count, 48, 220, TFT_WHITE, 3, middle_left);
  if (g_snapshot.last_report_id[0])
    text(g_snapshot.last_report_id, 48, 270, kGreen, 2, middle_left);
  else
    text("No Weather reports saved yet.", 48, 270, kMuted, 2, middle_left);
  button(48, 340, 310, 72, "SAVE SNAPSHOT",
         g_snapshot.sd_ready ? kCyan : kMuted);
  text("Saves JSON + CSV + HTML + SHA-256 manifest on SD.", 48, 455, TFT_WHITE, 1, middle_left);
  text("Raw IQ is never included by default.", 48, 490, kGreen, 1, middle_left);
}
void draw_body() {
  M5.Display.fillRect(0, kHeaderH, 1280, kTabsY - kHeaderH, kBg);
  switch (g_tab) {
    case Tab::now: draw_now(); break;
    case Tab::forecast: draw_forecast(); break;
    case Tab::map: draw_map(); break;
    case Tab::rf_weather: draw_rf(); break;
    case Tab::reports: draw_reports(); break;
    case Tab::count: break;
  }
}
void redraw() { draw_header(); draw_body(); draw_tabs(); }
}

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_tab = snapshot.tab;
  g_active = true;
  audio_header::reset(g_audio);
  focus_nav::begin_frame();
  M5.Display.fillScreen(kBg);
  redraw();
}
void leave() { g_active = false; }
void draw() { if (g_active) { focus_nav::begin_frame(); redraw(); } }
void update(const Snapshot& snapshot) {
  if (!g_active) return;
  const Tab prior = g_tab;
  g_snapshot = snapshot;
  g_snapshot.tab = prior;
  draw();
}
void select_tab(Tab tab_value) {
  if (tab_value >= Tab::count) return;
  g_tab = tab_value;
  g_snapshot.tab = tab_value;
  if (g_active) draw();
}
Tab tab() { return g_tab; }
bool active() { return g_active; }

Action handle_touch(int32_t x, int32_t y, uint32_t now_ms) {
  if (!g_active) return {};
  if (audio_header::home_hit(x, y)) return {ActionKind::exit_home, 0};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_settings, 0};
  if (audio_header::visualizer_hit(x, y))
    return {ActionKind::open_visualizer, 0};
  const auto aa = audio_header::handle_touch(g_audio, x, y, now_ms, g_snapshot.volume);
  if (aa == audio_header::Action::volume_set) {
    draw_header();
    return {ActionKind::volume_set, g_audio.volume};
  }
  if (aa == audio_header::Action::mute_toggle) {
    draw_header();
    return {ActionKind::mute_toggle, 0};
  }
  if (aa == audio_header::Action::opened || aa == audio_header::Action::closed) {
    draw_header();
    return {};
  }
  if (y >= kTabsY) {
    select_tab(static_cast<Tab>(std::clamp<int32_t>(x / kTabW, 0, 4)));
    return {};
  }
  if (g_tab == Tab::forecast && hit(x,y,48,390,280,64))
    return {ActionKind::cycle_online_policy,0};
  if (g_tab == Tab::rf_weather) {
    if (hit(x,y,48,280,160,64)) return {ActionKind::listen_noaa,0};
    if (hit(x,y,230,280,160,64)) return {ActionKind::scan_noaa,0};
    if (hit(x,y,48,362,342,58)) return {ActionKind::stop_rf,0};
  }
  if (g_tab == Tab::reports && hit(x,y,48,340,310,72))
    return {ActionKind::save_snapshot,0};
  return {};
}

bool self_check() {
  if (kTabsY != 630 || kTabW * static_cast<int>(Tab::count) != 1280) return false;
  Snapshot s{};
  s.online_policy = default_online_policy();
  if (s.online_policy != OnlinePolicy::disabled) return false;
  return static_cast<uint8_t>(Tab::reports) == 4;
}

}  // namespace orcsdr::weather

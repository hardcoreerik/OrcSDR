#include "weather_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "focus_nav.hpp"
#include "offline_map.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>

namespace orcsdr::weather {
namespace {
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr uint16_t kYellow = 0xff24;
constexpr int kTabsY = 630;
constexpr int kTabW = 256;
constexpr int kTabH = 90;

struct Rect { int x, y, w, h; };
constexpr Rect kPrev{42, 262, 120, 64};
constexpr Rect kNext{174, 262, 120, 64};
constexpr Rect kListen{314, 262, 230, 64};
constexpr Rect kScan{556, 262, 250, 64};
constexpr Rect kStop{818, 262, 190, 64};
constexpr Rect kSave{920, 154, 300, 72};

EXT_RAM_BSS_ATTR Snapshot g_snapshot{};
bool g_active = false;
Tab g_tab = Tab::now;

bool hit(int32_t x, int32_t y, const Rect& r) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void card(const Rect& r, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 10, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 10, border);
}

void button(const Rect& r, const char* label, bool selected = false, bool enabled = true) {
  orcsdr::focus_nav::note(r.x, r.y, r.w, r.h);
  const uint16_t border = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected ? 0x1264 : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  text(label, r.x + r.w / 2, r.y + r.h / 2, enabled ? TFT_WHITE : kMuted, 2);
}

void draw_header() {
  orcsdr::audio_header::draw_brand("WEATHER");
  M5.Display.drawFastVLine(350, 18, 58, kCyan);
  text("WEATHER", 390, 42, TFT_WHITE, 4, middle_left);
  if (g_snapshot.location_label[0])
    text(g_snapshot.location_label, 620, 42, kMuted, 2);
  orcsdr::audio_header::draw_battery(g_snapshot.battery_percent);
  orcsdr::audio_header::draw_home_button();
  orcsdr::audio_header::draw_mute_button(g_snapshot.sound_enabled);
  orcsdr::audio_header::draw_settings_button();
  M5.Display.drawFastHLine(20, 92, 1240, kGreen);
}

void draw_tabs() {
  constexpr const char* names[] = {"NOW", "FORECAST", "MAP", "RF WEATHER", "REPORTS"};
  for (int i = 0; i < 5; ++i)
    button({i * kTabW + 4, kTabsY + 4, kTabW - 8, kTabH - 8}, names[i],
           static_cast<int>(g_tab) == i);
}

void title(const char* name, const char* subtitle) {
  M5.Display.fillRect(0, 94, 1280, kTabsY - 94, TFT_BLACK);
  text(name, 32, 124, TFT_WHITE, 3, middle_left);
  text(subtitle, 34, 158, kMuted, 1, middle_left);
}

const char* freshness_name(Freshness f) {
  switch (f) {
    case Freshness::live: return "LIVE";
    case Freshness::recent: return "RECENT";
    case Freshness::stale: return "STALE";
    case Freshness::expired: return "EXPIRED";
    case Freshness::unavailable: return "--";
  }
  return "--";
}

const char* unit(ValueKind kind) {
  switch (kind) {
    case ValueKind::temperature_c: return "C";
    case ValueKind::humidity_percent: return "%";
    case ValueKind::pressure_hpa: return "hPa";
    case ValueKind::wind_speed_mps:
    case ValueKind::wind_gust_mps: return "m/s";
    case ValueKind::wind_direction_deg: return "deg";
    case ValueKind::rain_rate_mm_h: return "mm/h";
    case ValueKind::rain_total_mm: return "mm";
  }
  return "";
}

const char* value_name(ValueKind kind) {
  switch (kind) {
    case ValueKind::temperature_c: return "TEMPERATURE";
    case ValueKind::humidity_percent: return "HUMIDITY";
    case ValueKind::pressure_hpa: return "PRESSURE";
    case ValueKind::wind_speed_mps: return "WIND";
    case ValueKind::wind_gust_mps: return "GUST";
    case ValueKind::wind_direction_deg: return "WIND DIR";
    case ValueKind::rain_rate_mm_h: return "RAIN RATE";
    case ValueKind::rain_total_mm: return "RAIN TOTAL";
  }
  return "VALUE";
}

void draw_now() {
  title("NOW", "DIRECT / LOCAL / CACHED OBSERVATIONS - SOURCE AND AGE ARE ALWAYS SHOWN");
  if (g_snapshot.observation_count == 0) {
    card({34, 210, 1212, 220});
    text("NO CURRENT MEASUREMENT", 640, 284, kMuted, 4);
    text("Weather can still listen to NOAA Weather Radio from RF WEATHER.", 640, 342, TFT_WHITE, 2);
    text("No Internet request is made by opening this dashboard.", 640, 384, kGreen, 2);
    return;
  }
  const uint8_t count = std::min<uint8_t>(g_snapshot.observation_count, 6);
  for (uint8_t i = 0; i < count; ++i) {
    const int col = i % 2, row = i / 2;
    const Rect r{34 + col * 612, 188 + row * 136, 590, 118};
    card(r);
    const Observation& o = g_snapshot.observations[i];
    text(value_name(o.kind), r.x + 18, r.y + 24, kMuted, 1, middle_left);
    char line[72];
    if (o.valid)
      std::snprintf(line, sizeof(line), "%.1f %s", static_cast<double>(o.value), unit(o.kind));
    else
      std::snprintf(line, sizeof(line), "NO CURRENT MEASUREMENT");
    text(line, r.x + 18, r.y + 62, o.valid ? TFT_WHITE : kMuted, o.valid ? 3 : 2, middle_left);
    const Freshness fresh = classify_freshness(o, o.meta.age_seconds);
    std::snprintf(line, sizeof(line), "%s  %s  age %lus", source_label(o.meta.source),
                  freshness_name(fresh), static_cast<unsigned long>(o.meta.age_seconds));
    text(line, r.x + r.w - 18, r.y + 92, fresh == Freshness::expired ? kYellow : kGreen,
         1, middle_right);
  }
}

void draw_forecast() {
  title("FORECAST", "OFFLINE FIRST - NETWORK ENRICHMENT IS DISABLED BY DEFAULT");
  card({34, 194, 590, 340});
  text("LOCAL OUTLOOK", 64, 226, kCyan, 2, middle_left);
  text("Foundation does not invent a forecast from RF signal strength.", 64, 278, TFT_WHITE, 2, middle_left);
  text("Pressure/trend cards appear only when observations exist.", 64, 322, kMuted, 2, middle_left);
  card({646, 194, 600, 340});
  text("CACHED FORECAST", 676, 226, kCyan, 2, middle_left);
  char line[96];
  if (g_snapshot.cached_forecast_available) {
    std::snprintf(line, sizeof(line), "Cached copy - age %lus",
                  static_cast<unsigned long>(g_snapshot.cached_forecast_age_seconds));
    text(line, 676, 282, TFT_WHITE, 2, middle_left);
  } else {
    text("NO CACHED FORECAST", 676, 282, kMuted, 3, middle_left);
  }
  std::snprintf(line, sizeof(line), "ONLINE POLICY: %s",
                report::online_policy_key(g_snapshot.online_policy));
  text(line, 676, 350, g_snapshot.online_policy == report::OnlinePolicy::disabled ? kGreen : kYellow,
       2, middle_left);
  text("No automatic request is made in Foundation.", 676, 398, kMuted, 2, middle_left);
}

void draw_map() {
  title("MAP", "ORCMAPS / INSTALLED LOCAL DATA - NO NETWORK TILES REQUIRED");
  if (!g_snapshot.location_configured) {
    card({34, 206, 1212, 250});
    text("RECEIVER LOCATION NOT CONFIGURED", 640, 286, kYellow, 3);
    text("Set the shared receiver location in Settings to center Weather maps.", 640, 350, TFT_WHITE, 2);
    return;
  }
  if (!g_snapshot.map_available) {
    card({34, 206, 1212, 250});
    text("NO OFFLINE MAP PACK INSTALLED", 640, 286, kMuted, 3);
    text("Weather remains usable; RF and report data are still available.", 640, 350, TFT_WHITE, 2);
    return;
  }
  const offline_map::View view{static_cast<float>(g_snapshot.latitude_e7) / 1.0e7f,
                               static_cast<float>(g_snapshot.longitude_e7) / 1.0e7f,
                               25.0f, 34, 184, 1212, 410};
  offline_map::draw_base(view, 0x1450, kGrid, kCyan, kCyan);
  int x = 0, y = 0;
  if (offline_map::project(view, view.center_lat, view.center_lon, &x, &y)) {
    M5.Display.fillCircle(x, y, 7, kGreen);
    M5.Display.drawCircle(x, y, 12, TFT_WHITE);
  }
}

void draw_rf() {
  title("RF WEATHER", "ONE RTL-SDR - ONE RF JOB - NOAA LISTEN/SCAN IS EXPLICIT");
  card({34, 190, 990, 192}, g_snapshot.rf.rf_state == RfState::idle ? kCyan : kGreen);
  text("NOAA WEATHER RADIO", 58, 218, kCyan, 2, middle_left);
  char line[96];
  const uint32_t selected = noaa::channel_hz(g_snapshot.rf.selected_channel);
  std::snprintf(line, sizeof(line), "CH %u   %.3f MHz",
                static_cast<unsigned>(g_snapshot.rf.selected_channel + 1),
                static_cast<double>(selected) / 1.0e6);
  text(line, 58, 258, TFT_WHITE, 3, middle_left);
  const char* state = g_snapshot.rf.rf_state == RfState::listening ? "LISTENING"
                      : g_snapshot.rf.rf_state == RfState::scanning ? "SCANNING"
                                                                    : "IDLE";
  text(state, 982, 220, g_snapshot.rf.rf_state == RfState::idle ? kMuted : kGreen, 2, middle_right);
  std::snprintf(line, sizeof(line), "relative signal %.1f dBFS", static_cast<double>(g_snapshot.signal_dbfs));
  text(line, 982, 258, kMuted, 1, middle_right);
  button(kPrev, "CH -");
  button(kNext, "CH +");
  button(kListen, "LISTEN", g_snapshot.rf.rf_state == RfState::listening,
         g_snapshot.receiver_ready);
  button(kScan, "SCAN 7 CHANNELS", g_snapshot.rf.rf_state == RfState::scanning,
         g_snapshot.receiver_ready);
  button(kStop, "STOP", false, g_snapshot.rf.rf_state != RfState::idle);

  card({34, 398, 590, 190});
  text("LOCAL DATA", 58, 426, kCyan, 2, middle_left);
  text(g_snapshot.noaa_catalog_installed ? "NOAA CATALOG: INSTALLED" : "NOAA CATALOG: NOT INSTALLED",
       58, 472, g_snapshot.noaa_catalog_installed ? kGreen : kMuted, 2, middle_left);
  text(g_snapshot.map_available ? "ORCMAPS: READY" : "ORCMAPS: UNAVAILABLE",
       58, 512, g_snapshot.map_available ? kGreen : kMuted, 2, middle_left);
  text("SAME WATCH / sensors / radiosondes: PHASE 2", 58, 552, kMuted, 1, middle_left);

  card({646, 398, 600, 190});
  text("LAST NOAA SCAN", 670, 426, kCyan, 2, middle_left);
  if (g_snapshot.rf.scan_complete && g_snapshot.rf.strongest_frequency_hz) {
    std::snprintf(line, sizeof(line), "Strongest: %.3f MHz",
                  static_cast<double>(g_snapshot.rf.strongest_frequency_hz) / 1.0e6);
    text(line, 670, 472, TFT_WHITE, 2, middle_left);
    std::snprintf(line, sizeof(line), "%u / 7 channels sampled",
                  static_cast<unsigned>(g_snapshot.rf.scan_samples));
    text(line, 670, 512, kGreen, 2, middle_left);
  } else {
    text("NO COMPLETED SCAN", 670, 482, kMuted, 2, middle_left);
  }
}

void draw_reports() {
  title("REPORTS", "AUDITABLE SD HISTORY - DIRECT RF AND CACHED SOURCES STAY DISTINCT");
  card({34, 190, 1212, 330});
  text(g_snapshot.sd_ready ? "SD STORAGE READY" : "SD STORAGE UNAVAILABLE",
       62, 232, g_snapshot.sd_ready ? kGreen : kYellow, 2, middle_left);
  char line[96];
  std::snprintf(line, sizeof(line), "%u recent Weather report%s",
                static_cast<unsigned>(g_snapshot.report_count), g_snapshot.report_count == 1 ? "" : "s");
  text(line, 62, 282, TFT_WHITE, 2, middle_left);
  text("Snapshot reports record source, age, NOAA RF events and timestamps.", 62, 332, kMuted, 2, middle_left);
  text("Raw IQ is not recorded by default.", 62, 372, kMuted, 2, middle_left);
  button(kSave, "SAVE SNAPSHOT", false, g_snapshot.sd_ready);
  if (g_snapshot.report_status[0])
    text(g_snapshot.report_status, 62, 466, kCyan, 1, middle_left);
}

void draw_page() {
  switch (g_tab) {
    case Tab::now: draw_now(); break;
    case Tab::forecast: draw_forecast(); break;
    case Tab::map: draw_map(); break;
    case Tab::rf_weather: draw_rf(); break;
    case Tab::reports: draw_reports(); break;
  }
  draw_tabs();
}

}  // namespace

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_active = true;
  g_tab = Tab::now;
  draw();
}

void leave() { g_active = false; }

void draw() {
  if (!g_active) return;
  M5.Display.fillScreen(TFT_BLACK);
  draw_header();
  draw_page();
}

void update(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  if (g_active) draw_page();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (audio_header::home_hit(x, y)) return {ActionKind::exit_home};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_settings};
  if (audio_header::mute_hit(x, y)) return {ActionKind::sound_toggle};
  if (y >= kTabsY) {
    const int index = std::clamp(x / kTabW, 0, 4);
    select_tab(static_cast<Tab>(index));
    return {};
  }
  if (g_tab == Tab::rf_weather) {
    if (hit(x, y, kPrev)) return {ActionKind::noaa_channel_previous};
    if (hit(x, y, kNext)) return {ActionKind::noaa_channel_next};
    if (hit(x, y, kListen) && g_snapshot.receiver_ready) return {ActionKind::noaa_listen};
    if (hit(x, y, kScan) && g_snapshot.receiver_ready) return {ActionKind::noaa_scan};
    if (hit(x, y, kStop) && g_snapshot.rf.rf_state != RfState::idle) return {ActionKind::noaa_stop};
  }
  if (g_tab == Tab::reports && hit(x, y, kSave) && g_snapshot.sd_ready)
    return {ActionKind::save_snapshot};
  return {};
}

void select_tab(Tab tab_value) {
  if (static_cast<uint8_t>(tab_value) > static_cast<uint8_t>(Tab::reports)) return;
  g_tab = tab_value;
  if (g_active) draw_page();
}

Tab tab() { return g_tab; }
bool active() { return g_active; }

bool self_check() {
  return kTabsY + kTabH <= 720 && kTabW * 5 == 1280 &&
         kPrev.y + kPrev.h < kTabsY && kNext.y + kNext.h < kTabsY &&
         kListen.y + kListen.h < kTabsY && kScan.y + kScan.h < kTabsY &&
         kStop.y + kStop.h < kTabsY && kSave.y + kSave.h < kTabsY;
}

}  // namespace orcsdr::weather

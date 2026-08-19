#include "settings_app.hpp"

#include <M5Unified.h>
#include <esp_attr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::settings {
namespace {

constexpr uint16_t kBg = 0x0841;
constexpr uint16_t kPanel = 0x1082;
constexpr uint16_t kBlue = 0x04ff;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kMuted = 0x9cf3;
constexpr int kHeaderH = 72;
constexpr int kRailW = 286;
constexpr int kRailY = 82;
constexpr int kRailRowH = 72;
constexpr uint8_t kSettingsMinTextSize = 2;
constexpr uint16_t kRanges[] = {10, 25, 50, 100};
constexpr uint16_t kTimeouts[] = {0, 30, 60, 120, 300};
constexpr const char* kLabels[] = {
    "CONNECTIVITY", "LOCATION & ADS-B", "DATA & MAPS", "DISPLAY & AUDIO",
    "RADIO DEFAULTS", "STORAGE", "COMPANION", "SYSTEM"};

static_assert(static_cast<uint8_t>(Section::count) == std::size(kLabels));
static_assert(std::size(kRanges) == 4 && kRanges[0] == 10 && kRanges[3] == 100);

enum class EditField : uint8_t { none, latitude, longitude };
enum class WifiEdit : uint8_t { none, ssid, password };

EXT_RAM_BSS_ATTR State g_state;
Section g_section = Section::connectivity;
EditField g_edit = EditField::none;
bool g_active = false;
bool g_latitude_set = false;
bool g_longitude_set = false;
char g_entry[20]{};
WifiEdit g_wifi_edit = WifiEdit::none;
bool g_wifi_shift = false;
bool g_wifi_symbols = false;
bool g_wifi_show_password = false;
char g_wifi_edit_ssid[33]{};
char g_wifi_edit_password[64]{};
char g_wifi_request_ssid[33]{};
char g_wifi_request_password[64]{};
bool g_wifi_request_pending = false;
int8_t g_catalog_remove_armed = -1;

bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color, uint8_t size = 2,
          textdatum_t datum = middle_left) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(std::max(size, kSettingsMinTextSize));
  M5.Display.setTextColor(color, kBg);
  M5.Display.drawString(value, x, y);
}

void button(const char* label, int x, int y, int w, int h, uint16_t fill) {
  M5.Display.fillRoundRect(x, y, w, h, 8, fill);
  M5.Display.drawRoundRect(x, y, w, h, 8, TFT_LIGHTGREY);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, fill);
  M5.Display.drawString(label, x + w / 2, y + h / 2);
}

void value_row(const char* label, const char* value, int y,
               uint16_t color = TFT_WHITE) {
  text(label, 330, y, kMuted, 2);
  text(value, 1218, y, color, 2, middle_right);
  M5.Display.drawFastHLine(330, y + 31, 888, 0x2945);
}

void draw_header() {
  M5.Display.fillRect(0, 0, 1280, kHeaderH, TFT_BLACK);
  text("OrcSDR", 28, 36, TFT_WHITE, 3);
  text("SETTINGS", 180, 36, kBlue, 3);
  char status[96];
  if (g_state.wifi_connected)
    snprintf(status, sizeof(status), "%s  %s", g_state.wifi_ssid, g_state.wifi_ip);
  else
    strlcpy(status, !g_state.wifi_power_enabled ? "Wi-Fi powered off"
                    : g_state.wifi_connecting ? "Wi-Fi connecting"
                    : g_state.wifi_scanning ? "Wi-Fi scanning" : "Wi-Fi offline",
            sizeof(status));
  text(status, 1020, 36, g_state.wifi_connected ? kGreen : kMuted, 2, middle_right);
  button("CLOSE", 1090, 13, 162, 46, TFT_MAROON);
}

void draw_rail() {
  M5.Display.fillRect(0, kHeaderH, kRailW, 720 - kHeaderH, TFT_BLACK);
  for (uint8_t i = 0; i < static_cast<uint8_t>(Section::count); ++i) {
    const int y = kRailY + i * kRailRowH;
    const bool selected = static_cast<uint8_t>(g_section) == i;
    const uint16_t fill = selected ? TFT_DARKCYAN : 0x1082;
    M5.Display.fillRoundRect(12, y, kRailW - 24, kRailRowH - 8, 7, fill);
    M5.Display.setTextDatum(middle_left);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(selected ? TFT_WHITE : kMuted, fill);
    M5.Display.drawString(kLabels[i], 30, y + (kRailRowH - 8) / 2);
  }
}

void draw_connectivity() {
  text("CONNECTIVITY", 330, 115, kBlue, 3);
  char value[96];
  const char* status = !g_state.wifi_power_enabled ? "POWERED OFF"
                       : g_state.wifi_connected ? "CONNECTED"
                       : g_state.wifi_connecting ? "CONNECTING"
                                                : "OFFLINE";
  const uint16_t status_color = !g_state.wifi_power_enabled ? TFT_ORANGE
                                : g_state.wifi_connected ? kGreen
                                : g_state.wifi_connecting ? TFT_YELLOW
                                                         : TFT_ORANGE;
  snprintf(value, sizeof(value), "%s  %s  %s", status,
           g_state.wifi_ssid[0] ? g_state.wifi_ssid : "--",
           g_state.wifi_connected ? g_state.wifi_ip : "");
  text(value, 330, 155, status_color, 2);
  if (g_state.wifi_message[0]) text(g_state.wifi_message, 1218, 155, kMuted, 1, middle_right);
  button(g_state.wifi_scanning ? "SCANNING..." : "SCAN", 330, 180, 170, 46,
         g_state.wifi_scanning ? TFT_DARKGREY : TFT_DARKCYAN);
  button("ADD HIDDEN", 520, 180, 210, 46, TFT_NAVY);
  button(g_state.wifi_power_enabled ? "POWER OFF" : "POWER ON", 750, 180, 170, 46,
         g_state.wifi_power_enabled ? TFT_MAROON : TFT_DARKGREEN);
  text("WI-FI ANTENNA", 330, 245, kMuted, 2);
  button(g_state.wifi_external_antenna ? "EXTERNAL (MMCX)" : "INTERNAL",
         820, 218, 398, 54,
         g_state.wifi_external_antenna ? TFT_DARKGREEN : TFT_NAVY);

  text("SAVED NETWORKS (PRIORITY ORDER)", 330, 290, kBlue, 2);
  for (uint8_t i = 0; i < g_state.saved_network_count && i < 4; ++i) {
    const int y = 318 + i * 52;
    snprintf(value, sizeof(value), "%u  %.24s%s", i + 1, g_state.profiles[i].ssid,
             g_state.profiles[i].connected ? "  CONNECTED" : "");
    text(value, 340, y + 23, g_state.profiles[i].connected ? kGreen : TFT_WHITE, 2);
    button("USE", 720, y, 90, 44, TFT_DARKCYAN);
    button("UP", 820, y, 76, 44, i ? TFT_NAVY : TFT_DARKGREY);
    button("DOWN", 906, y, 90, 44,
           i + 1 < g_state.saved_network_count ? TFT_NAVY : TFT_DARKGREY);
    button("FORGET", 1006, y, 170, 44, TFT_MAROON);
  }
  if (g_state.saved_network_count == 0)
    text("NO SAVED NETWORKS", 340, 342, kMuted, 2);

  text("AVAILABLE NETWORKS", 330, 548, kBlue, 2);
  const uint8_t shown = std::min<uint8_t>(g_state.network_count, 6);
  for (uint8_t i = 0; i < shown; ++i) {
    const int x = 330 + (i % 2) * 428;
    const int y = 572 + (i / 2) * 42;
    snprintf(value, sizeof(value), "%.18s  %d%s%s", g_state.networks[i].ssid,
             g_state.networks[i].rssi, g_state.networks[i].secure ? "  LOCK" : "  OPEN",
             g_state.networks[i].saved ? "  SAVED" : "");
    button(value, x, y, 418, 36,
           g_state.networks[i].saved ? 0x2945 : TFT_DARKCYAN);
  }
}

void draw_location() {
  text("LOCATION & ADS-B", 330, 115, kBlue, 3);
  char value[48];
  value_row("PROFILE LABEL", g_state.location_label[0] ? g_state.location_label : "NOT SET",
            165);
  snprintf(value, sizeof(value), g_state.location_configured ? "%.7f" : "NOT SET",
           g_state.latitude_e7 / 10000000.0);
  text("RECEIVER LATITUDE", 330, 230, kMuted, 2);
  button(value, 820, 204, 398, 54, TFT_NAVY);
  snprintf(value, sizeof(value), g_state.location_configured ? "%.7f" : "NOT SET",
           g_state.longitude_e7 / 10000000.0);
  text("RECEIVER LONGITUDE", 330, 300, kMuted, 2);
  button(value, 820, 274, 398, 54, TFT_NAVY);
  snprintf(value, sizeof(value), "%u NM", g_state.radar_range_nm);
  text("RADAR RANGE", 330, 370, kMuted, 2);
  button(value, 820, 344, 398, 54, TFT_DARKCYAN);
  value_row("MAP PACK", g_state.map_pack[0] ? g_state.map_pack : "NOT INSTALLED", 445);
  value_row("RF GAIN", "AUTO (READ ONLY)", 495, kMuted);
  text("Phone GPS proposals require confirmation on this screen.", 330, 565,
       TFT_LIGHTGREY, 2);
}

void draw_data_maps() {
  text("DATA & MAPS", 330, 115, kBlue, 3);
  char catalog[112];
  snprintf(catalog, sizeof(catalog), "%s%s", g_state.catalog_ready ? "SIGNED CATALOG " : "NO CATALOG ",
           g_state.catalog_date[0] ? g_state.catalog_date : "CHECK MANUALLY");
  text(catalog, 330, 153, g_state.catalog_ready ? kGreen : kMuted, 2);
  button(g_state.catalog_busy ? "WORKING..." : "CHECK FOR UPDATES", 940, 126, 278, 48,
         g_state.catalog_busy ? TFT_DARKGREY : TFT_DARKCYAN);
  if (g_state.catalog_message[0]) text(g_state.catalog_message, 330, 180, kMuted, 2);
  for (uint8_t i = 0; i < 5; ++i) {
    const auto& pack = g_state.catalog_packs[i];
    const int y = 185 + i * 96;
    M5.Display.fillRoundRect(330, y, 888, 88, 8, kPanel);
    M5.Display.drawRoundRect(330, y, 888, 88, 8, kBlue);
    text(pack.title[0] ? pack.title : "DATA PACK", 350, y + 24, kBlue, 3);
    char detail[96];
    snprintf(detail, sizeof(detail), "v%s  source %s  %.1f + %.1f MB",
             pack.version[0] ? pack.version : "--", pack.source_date[0] ? pack.source_date : "--",
             pack.runtime_bytes / 1048576.0, pack.archive_bytes / 1048576.0);
    text(detail, 350, y + 47, TFT_WHITE, 1);
    text(pack.status[0] ? pack.status : "CHECK CATALOG", 350, y + 70,
         pack.installed ? kGreen : kMuted, 1);
    const char* install = pack.installed ? (pack.update_available ? "UPDATE" : "REINSTALL") : "INSTALL";
    button(install, 930, y + 14, 132, 42,
           g_state.catalog_busy || !g_state.catalog_ready ? TFT_DARKGREY : TFT_DARKCYAN);
    button(g_catalog_remove_armed == i ? "CONFIRM" : "REMOVE", 1072, y + 14, 126, 42,
           pack.installed && !g_state.catalog_busy ? TFT_MAROON : TFT_DARKGREY);
  }
  text("Manual only. Downloads keep reception active.", 330, 688, TFT_LIGHTGREY, 1);
}

void draw_display_audio() {
  text("DISPLAY & AUDIO", 330, 115, kBlue, 3);
  char value[32];
  snprintf(value, sizeof(value), "%u / 255", g_state.brightness);
  value_row("BRIGHTNESS", value, 180);
  button("-", 850, 205, 90, 48, TFT_DARKGREY);
  button("+", 960, 205, 90, 48, TFT_DARKCYAN);
  snprintf(value, sizeof(value), g_state.screen_timeout_sec ? "%u SEC" : "NEVER",
           g_state.screen_timeout_sec);
  value_row("SCREEN TIMEOUT", value, 300);
  button("CYCLE", 960, 325, 160, 48, TFT_DARKCYAN);
  snprintf(value, sizeof(value), "%u / 255", g_state.volume);
  value_row("MASTER VOLUME", value, 420);
  button("-", 850, 445, 90, 48, TFT_DARKGREY);
  button("+", 960, 445, 90, 48, TFT_DARKCYAN);
  value_row("DEFAULT SOUND", g_state.sound_default ? "ON" : "OFF", 555,
            g_state.sound_default ? kGreen : kMuted);
  button("TOGGLE", 960, 580, 160, 48, TFT_DARKCYAN);
  value_row("SCREEN ORIENTATION", g_state.rotation == 3 ? "LANDSCAPE 180" : "LANDSCAPE", 635);
  button("ROTATE", 960, 660, 160, 48, TFT_DARKCYAN);
}

void draw_radio_defaults() {
  text("RADIO DEFAULTS", 330, 115, kBlue, 3);
  char value[40];
  value_row("STARTUP DESTINATION", "LAST RADIO", 175);
  value_row("AUTO-START RECEPTION", g_state.auto_start_reception ? "ON" : "OFF", 225);
  value_row("LAST / DEFAULT BAND", g_state.default_band, 275);
  snprintf(value, sizeof(value), "%.3f MHz", g_state.fm_frequency_hz / 1000000.0);
  value_row("FM FREQUENCY", value, 325);
  value_row("GRAPHICS DEFAULT", g_state.graphics_default ? "ON" : "OFF", 375);
  value_row("GAIN / BIAS-TEE / CAL", "UNAVAILABLE", 425, kMuted);
  button("TOGGLE AUTO-START", 330, 500, 260, 50, TFT_DARKCYAN);
  button("TOGGLE GRAPHICS", 620, 500, 240, 50, TFT_DARKCYAN);
}

void draw_storage() {
  text("STORAGE", 330, 115, kBlue, 3);
  char value[48];
  value_row("SD HEALTH", g_state.sd_ready ? "READY" : "NOT MOUNTED", 175,
            g_state.sd_ready ? kGreen : TFT_ORANGE);
  snprintf(value, sizeof(value), "%.1f / %.1f GB FREE",
           g_state.sd_free_bytes / 1073741824.0, g_state.sd_total_bytes / 1073741824.0);
  value_row("CAPACITY", g_state.sd_ready ? value : "--", 225);
  value_row("DATABASES", "CALCULATING ON DEMAND", 275, kMuted);
  value_row("MAPS", "CALCULATING ON DEMAND", 325, kMuted);
  value_row("RECORDINGS / LOGS", "CALCULATING ON DEMAND", 375, kMuted);
  text("Targeted deletion requires hold and confirmation; partitioning stays in M5Launcher.",
       330, 470, TFT_LIGHTGREY, 2);
}

void draw_companion() {
  text("COMPANION", 330, 115, kBlue, 3);
  value_row("WEB CONSOLE", g_state.web_console_enabled ? "ON" : "OFF", 175,
            g_state.web_console_enabled ? kGreen : kMuted);
  button(g_state.web_console_enabled ? "DISABLE" : "ENABLE", 960, 150, 170, 48,
         g_state.web_console_enabled ? TFT_MAROON : TFT_DARKGREEN);
  value_row("URL",
            g_state.web_console_listening && g_state.web_console_url[0]
                ? g_state.web_console_url
                : "OFFLINE",
            225, g_state.web_console_listening ? kGreen : kMuted);
  value_row("LOCAL DISCOVERY",
            g_state.web_console_listening ? "orcsdr.local" : "NOT ENABLED", 275,
            g_state.web_console_listening ? kGreen : kMuted);
  value_row("PHONE CONNECTION", "OPTIONAL", 325, kGreen);
  value_row("BLUETOOTH", g_state.companion_supported ? "AVAILABLE" : "FEASIBILITY PENDING",
            375, kMuted);
  text("LAN read-only page for Android TV. No passwords, location, or control.",
       330, 470, TFT_LIGHTGREY, 2);
  text("OrcSDR remains fully usable with no phone, BLE, GPS, or HIVE.", 330, 510,
       TFT_LIGHTGREY, 2);
}

void draw_system_power() {
  M5.Display.fillRect(330, 160, 890, 250, kBg);
  char value[48];
  if (g_state.battery_mv >= 0) {
    snprintf(value, sizeof(value), "%d mV  /  %ld%%", g_state.battery_mv,
             static_cast<long>(g_state.battery_level));
  } else {
    strlcpy(value, "UNAVAILABLE", sizeof(value));
  }
  value_row("BATTERY RAIL", value, 175, g_state.battery_mv >= 0 ? kGreen : TFT_ORANGE);
  value_row("CHARGE STATE", g_state.charging_state[0] ? g_state.charging_state : "UNKNOWN", 225);
  snprintf(value, sizeof(value), "%ld mA (RAW)",
           static_cast<long>(g_state.battery_current_ma));
  value_row("BATTERY CURRENT", value, 275);
  if (g_state.vbus_mv >= 0)
    snprintf(value, sizeof(value), "%d mV", g_state.vbus_mv);
  else
    strlcpy(value, "NOT EXPOSED", sizeof(value));
  value_row("USB / VBUS", value, 325, g_state.vbus_mv >= 0 ? kGreen : kMuted);
  value_row("EXTERNAL 7.4 V", "NO SEPARATE SENSOR", 375, kMuted);
}

void draw_system() {
  text("SYSTEM", 330, 115, kBlue, 3);
  char value[40];
  draw_system_power();
  value_row("BUILD", g_state.build_identity, 435);
  snprintf(value, sizeof(value), "%lu SEC", static_cast<unsigned long>(g_state.uptime_seconds));
  value_row("UPTIME", value, 475);
  value_row("NETWORK", g_state.wifi_connected ? "CONNECTED" : "OFFLINE", 515);
  value_row("SD", g_state.sd_ready ? "READY" : "UNAVAILABLE", 555);
  text("Reboot, reset, export, and Launcher handoff require separate safety gates.",
       330, 605, TFT_LIGHTGREY, 2);
}

void draw_content() {
  M5.Display.fillRect(kRailW, kHeaderH, 1280 - kRailW, 720 - kHeaderH, kBg);
  switch (g_section) {
    case Section::connectivity: draw_connectivity(); break;
    case Section::location_adsb: draw_location(); break;
    case Section::data_maps: draw_data_maps(); break;
    case Section::display_audio: draw_display_audio(); break;
    case Section::radio_defaults: draw_radio_defaults(); break;
    case Section::storage: draw_storage(); break;
    case Section::companion: draw_companion(); break;
    case Section::system: draw_system(); break;
    default: break;
  }
}

void start_edit(EditField field) {
  g_edit = field;
  const int32_t value = field == EditField::latitude ? g_state.latitude_e7
                                                     : g_state.longitude_e7;
  snprintf(g_entry, sizeof(g_entry), "%.7f", value / 10000000.0);
}

void draw_keypad() {
  M5.Display.fillRect(kRailW, kHeaderH, 1280 - kRailW, 720 - kHeaderH, kBg);
  text(g_edit == EditField::latitude ? "EDIT LATITUDE" : "EDIT LONGITUDE",
       330, 112, kBlue, 3);
  button(g_entry[0] ? g_entry : "0", 360, 150, 820, 58, TFT_NAVY);
  static constexpr const char* keys[] = {"1", "2", "3", "4", "5", "6", "7",
                                          "8", "9", "-", "0", ".", "BACK"};
  for (int i = 0; i < 13; ++i) {
    const int col = i % 4, row = i / 4;
    button(keys[i], 360 + col * 205, 235 + row * 68, 185, 54, TFT_DARKGREY);
  }
  button("CANCEL", 360, 515, 385, 58, TFT_MAROON);
  button("SAVE", 795, 515, 385, 58, TFT_DARKGREEN);
}

void begin_wifi_edit(WifiEdit edit, const char* ssid = nullptr) {
  g_wifi_edit = edit;
  g_wifi_shift = false;
  g_wifi_symbols = false;
  g_wifi_show_password = false;
  if (ssid) strlcpy(g_wifi_edit_ssid, ssid, sizeof(g_wifi_edit_ssid));
  else g_wifi_edit_ssid[0] = '\0';
  g_wifi_edit_password[0] = '\0';
}

const char* wifi_key_row(int row) {
  static constexpr const char* kNormal[] = {
      "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
  static constexpr const char* kShift[] = {
      "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  static constexpr const char* kSymbols[] = {
      "!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>?/`~", ""};
  return g_wifi_symbols ? kSymbols[row] : (g_wifi_shift ? kShift[row] : kNormal[row]);
}

void draw_wifi_keyboard() {
  M5.Display.fillRect(kRailW, kHeaderH, 1280 - kRailW, 720 - kHeaderH, kBg);
  text(g_wifi_edit == WifiEdit::ssid ? "HIDDEN NETWORK SSID" : "WI-FI PASSWORD",
       330, 108, kBlue, 3);
  char shown[64]{};
  const char* entry = g_wifi_edit == WifiEdit::ssid ? g_wifi_edit_ssid
                                                    : g_wifi_edit_password;
  if (g_wifi_edit == WifiEdit::password && !g_wifi_show_password) {
    const size_t count = std::min(strlen(entry), sizeof(shown) - 1);
    memset(shown, '*', count);
  } else {
    strlcpy(shown, entry, sizeof(shown));
  }
  button(shown[0] ? shown : " ", 330, 140, 846, 56, TFT_NAVY);

  for (int row = 0; row < 4; ++row) {
    const char* keys = wifi_key_row(row);
    const int count = static_cast<int>(strlen(keys));
    if (count == 0) continue;
    const int gap = 6;
    const int key_w = (846 - (count - 1) * gap) / count;
    for (int col = 0; col < count; ++col) {
      char label[2] = {keys[col], '\0'};
      button(label, 330 + col * (key_w + gap), 215 + row * 62,
             key_w, 50, TFT_DARKGREY);
    }
  }
  button(g_wifi_shift ? "SHIFT ON" : "SHIFT", 330, 475, 150, 46, TFT_DARKCYAN);
  button(g_wifi_symbols ? "LETTERS" : "SYMBOLS", 492, 475, 170, 46, TFT_DARKCYAN);
  button("SPACE", 674, 475, 170, 46, TFT_DARKGREY);
  button("BACK", 856, 475, 150, 46, TFT_DARKGREY);
  if (g_wifi_edit == WifiEdit::password)
    button(g_wifi_show_password ? "HIDE" : "SHOW", 1018, 475, 158, 46, TFT_NAVY);
  button("CANCEL", 330, 550, 250, 54, TFT_MAROON);
  button(g_wifi_edit == WifiEdit::ssid ? "NEXT" : "CONNECT",
         926, 550, 250, 54, TFT_DARKGREEN);
}

Action queue_wifi_request(const char* ssid, const char* password) {
  if (!ssid || !ssid[0]) return {};
  strlcpy(g_wifi_request_ssid, ssid, sizeof(g_wifi_request_ssid));
  strlcpy(g_wifi_request_password, password ? password : "",
          sizeof(g_wifi_request_password));
  memset(g_wifi_edit_password, 0, sizeof(g_wifi_edit_password));
  g_wifi_request_pending = true;
  g_wifi_edit = WifiEdit::none;
  draw_content();
  return {ActionKind::connect_wifi, 0};
}

Action handle_wifi_keyboard(int x, int y) {
  if (hit(x, y, 330, 550, 250, 54)) {
    memset(g_wifi_edit_password, 0, sizeof(g_wifi_edit_password));
    g_wifi_edit = WifiEdit::none;
    draw_content();
    return {};
  }
  if (hit(x, y, 926, 550, 250, 54)) {
    if (g_wifi_edit == WifiEdit::ssid) {
      if (!g_wifi_edit_ssid[0]) return {};
      g_wifi_edit = WifiEdit::password;
      draw_wifi_keyboard();
      return {};
    }
    return queue_wifi_request(g_wifi_edit_ssid, g_wifi_edit_password);
  }
  if (hit(x, y, 330, 475, 150, 46)) {
    g_wifi_shift = !g_wifi_shift;
    g_wifi_symbols = false;
    draw_wifi_keyboard();
    return {};
  }
  if (hit(x, y, 492, 475, 170, 46)) {
    g_wifi_symbols = !g_wifi_symbols;
    draw_wifi_keyboard();
    return {};
  }
  char* entry = g_wifi_edit == WifiEdit::ssid ? g_wifi_edit_ssid
                                               : g_wifi_edit_password;
  const size_t capacity = g_wifi_edit == WifiEdit::ssid
                              ? sizeof(g_wifi_edit_ssid)
                              : sizeof(g_wifi_edit_password);
  size_t length = strlen(entry);
  if (hit(x, y, 674, 475, 170, 46)) {
    if (length + 1 < capacity) entry[length++] = ' ', entry[length] = '\0';
    draw_wifi_keyboard();
    return {};
  }
  if (hit(x, y, 856, 475, 150, 46)) {
    if (length) entry[length - 1] = '\0';
    draw_wifi_keyboard();
    return {};
  }
  if (g_wifi_edit == WifiEdit::password && hit(x, y, 1018, 475, 158, 46)) {
    g_wifi_show_password = !g_wifi_show_password;
    draw_wifi_keyboard();
    return {};
  }
  for (int row = 0; row < 4; ++row) {
    const char* keys = wifi_key_row(row);
    const int count = static_cast<int>(strlen(keys));
    if (count == 0) continue;
    const int gap = 6;
    const int key_w = (846 - (count - 1) * gap) / count;
    for (int col = 0; col < count; ++col) {
      if (!hit(x, y, 330 + col * (key_w + gap), 215 + row * 62, key_w, 50)) continue;
      if (length + 1 < capacity) {
        entry[length] = keys[col];
        entry[length + 1] = '\0';
      }
      if (g_wifi_shift && !g_wifi_symbols) g_wifi_shift = false;
      draw_wifi_keyboard();
      return {};
    }
  }
  return {};
}

bool valid_coordinate(EditField field, double value) {
  return std::isfinite(value) &&
         (field == EditField::latitude ? value >= -90.0 && value <= 90.0
                                       : value >= -180.0 && value <= 180.0);
}

Action handle_keypad(int x, int y) {
  if (hit(x, y, 360, 515, 385, 58)) {
    g_edit = EditField::none;
    draw_content();
    return {};
  }
  if (hit(x, y, 795, 515, 385, 58)) {
    char* end = nullptr;
    const double value = strtod(g_entry, &end);
    if (end != g_entry && *end == '\0' && valid_coordinate(g_edit, value)) {
      const int32_t e7 = static_cast<int32_t>(llround(value * 10000000.0));
      if (g_edit == EditField::latitude) {
        g_state.latitude_e7 = e7;
        g_latitude_set = true;
      } else {
        g_state.longitude_e7 = e7;
        g_longitude_set = true;
      }
      g_state.location_configured = g_latitude_set && g_longitude_set;
      g_edit = EditField::none;
      draw_content();
      return {ActionKind::location_changed, 0};
    }
    return {};
  }
  static constexpr char keys[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9',
                                   '-', '0', '.', '\b'};
  for (int i = 0; i < 13; ++i) {
    const int col = i % 4, row = i / 4;
    if (!hit(x, y, 360 + col * 205, 235 + row * 68, 185, 54)) continue;
    const size_t length = strlen(g_entry);
    if (keys[i] == '\b') {
      if (length) g_entry[length - 1] = '\0';
    } else if (length + 1 < sizeof(g_entry)) {
      g_entry[length] = keys[i];
      g_entry[length + 1] = '\0';
    }
    draw_keypad();
    return {};
  }
  return {};
}

template <typename T, size_t N>
T next_value(T current, const T (&values)[N]) {
  size_t index = 0;
  while (index < N && values[index] != current) ++index;
  return values[(index + 1) % N];
}

}  // namespace

void enter(const State& state_value, Section section) {
  g_state = state_value;
  g_section = section;
  g_edit = EditField::none;
  g_wifi_edit = WifiEdit::none;
  g_catalog_remove_armed = -1;
  g_active = true;
  g_latitude_set = state_value.location_configured;
  g_longitude_set = state_value.location_configured;
  draw();
}

void leave() {
  g_active = false;
  g_edit = EditField::none;
  g_wifi_edit = WifiEdit::none;
}

void draw() {
  M5.Display.fillScreen(kBg);
  draw_header();
  draw_rail();
  draw_content();
}

void update(const State& state_value) {
  const bool header_changed = g_state.wifi_power_enabled != state_value.wifi_power_enabled ||
                              g_state.wifi_external_antenna != state_value.wifi_external_antenna ||
                              g_state.wifi_connected != state_value.wifi_connected ||
                              g_state.wifi_connecting != state_value.wifi_connecting ||
                              g_state.wifi_scanning != state_value.wifi_scanning ||
                              strcmp(g_state.wifi_ssid, state_value.wifi_ssid) != 0 ||
                              strcmp(g_state.wifi_ip, state_value.wifi_ip) != 0;
  const bool page_changed = g_section == Section::connectivity &&
                            (g_state.wifi_power_enabled != state_value.wifi_power_enabled ||
                             g_state.wifi_external_antenna != state_value.wifi_external_antenna ||
                             g_state.wifi_scanning != state_value.wifi_scanning ||
                             g_state.wifi_connecting != state_value.wifi_connecting ||
                             g_state.network_count != state_value.network_count ||
                             g_state.saved_network_count != state_value.saved_network_count ||
                             g_state.wifi_rssi != state_value.wifi_rssi ||
                             memcmp(g_state.networks, state_value.networks,
                                    sizeof(g_state.networks)) != 0 ||
                             strcmp(g_state.wifi_message, state_value.wifi_message) != 0 ||
                             memcmp(g_state.profiles, state_value.profiles,
                                    sizeof(g_state.profiles)) != 0);
  const bool power_changed = g_section == Section::system &&
                             (g_state.battery_level != state_value.battery_level ||
                              g_state.battery_mv != state_value.battery_mv ||
                              g_state.battery_current_ma != state_value.battery_current_ma ||
                              g_state.vbus_mv != state_value.vbus_mv ||
                              strcmp(g_state.charging_state, state_value.charging_state) != 0);
  const bool catalog_changed = g_section == Section::data_maps &&
      (g_state.catalog_ready != state_value.catalog_ready || g_state.catalog_busy != state_value.catalog_busy ||
       strcmp(g_state.catalog_message, state_value.catalog_message) != 0 ||
       strcmp(g_state.catalog_date, state_value.catalog_date) != 0 ||
       memcmp(g_state.catalog_packs, state_value.catalog_packs, sizeof(g_state.catalog_packs)) != 0);
  const bool companion_changed = g_section == Section::companion &&
      (g_state.web_console_enabled != state_value.web_console_enabled ||
       g_state.web_console_listening != state_value.web_console_listening ||
       strcmp(g_state.web_console_url, state_value.web_console_url) != 0);
  g_state = state_value;
  if (header_changed) draw_header();
  if (page_changed && g_edit == EditField::none && g_wifi_edit == WifiEdit::none)
    draw_content();
  if (power_changed && g_edit == EditField::none && g_wifi_edit == WifiEdit::none)
    draw_system_power();
  if (catalog_changed && g_edit == EditField::none && g_wifi_edit == WifiEdit::none)
    draw_content();
  if (companion_changed && g_edit == EditField::none && g_wifi_edit == WifiEdit::none)
    draw_content();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (g_wifi_edit != WifiEdit::none) return handle_wifi_keyboard(x, y);
  if (g_edit != EditField::none) return handle_keypad(x, y);
  if (hit(x, y, 1090, 13, 162, 46)) {
    g_active = false;
    return {ActionKind::close, 0};
  }
  if (x < kRailW && y >= kRailY) {
    const int index = (y - kRailY) / kRailRowH;
    if (index >= 0 && index < static_cast<int>(Section::count)) {
      g_section = static_cast<Section>(index);
      draw_rail();
      draw_content();
    }
    return {};
  }
  if (g_section == Section::connectivity) {
    if (hit(x, y, 750, 180, 170, 46))
      return {ActionKind::wifi_power_changed, g_state.wifi_power_enabled ? 0 : 1};
    if (hit(x, y, 820, 218, 398, 54))
      return {ActionKind::wifi_antenna_changed, g_state.wifi_external_antenna ? 0 : 1};
    if (!g_state.wifi_power_enabled) return {};
    if (hit(x, y, 330, 180, 170, 46) && !g_state.wifi_scanning)
      return {ActionKind::scan_wifi, 0};
    if (hit(x, y, 520, 180, 210, 46)) {
      begin_wifi_edit(WifiEdit::ssid);
      draw_wifi_keyboard();
      return {};
    }
    for (uint8_t i = 0; i < g_state.saved_network_count && i < 4; ++i) {
      const int row_y = 318 + i * 52;
      if (hit(x, y, 720, row_y, 90, 44))
        return {ActionKind::connect_saved_wifi, i};
      if (i && hit(x, y, 820, row_y, 76, 44))
        return {ActionKind::move_wifi_up, i};
      if (i + 1 < g_state.saved_network_count && hit(x, y, 906, row_y, 90, 44))
        return {ActionKind::move_wifi_down, i};
      if (hit(x, y, 1006, row_y, 170, 44))
        return {ActionKind::forget_wifi, i};
    }
    for (uint8_t i = 0; i < std::min<uint8_t>(g_state.network_count, 6); ++i) {
      const int row_x = 330 + (i % 2) * 428;
      const int row_y = 572 + (i / 2) * 42;
      if (!hit(x, y, row_x, row_y, 418, 36)) continue;
      if (g_state.networks[i].saved) {
        for (uint8_t saved = 0; saved < g_state.saved_network_count; ++saved)
          if (strcmp(g_state.networks[i].ssid, g_state.profiles[saved].ssid) == 0)
            return {ActionKind::connect_saved_wifi, saved};
        return {};
      }
      if (!g_state.networks[i].secure)
        return queue_wifi_request(g_state.networks[i].ssid, "");
      begin_wifi_edit(WifiEdit::password, g_state.networks[i].ssid);
      draw_wifi_keyboard();
      return {};
    }
  } else if (g_section == Section::location_adsb) {
    if (hit(x, y, 820, 204, 398, 54)) { start_edit(EditField::latitude); draw_keypad(); }
    else if (hit(x, y, 820, 274, 398, 54)) { start_edit(EditField::longitude); draw_keypad(); }
    else if (hit(x, y, 820, 344, 398, 54)) {
      g_state.radar_range_nm = next_value(g_state.radar_range_nm, kRanges);
      draw_content();
      return {ActionKind::range_changed, g_state.radar_range_nm};
    }
  } else if (g_section == Section::data_maps) {
    if (hit(x, y, 940, 126, 278, 48) && !g_state.catalog_busy)
      return {ActionKind::catalog_check, 0};
    for (uint8_t i = 0; i < 5; ++i) {
      const int row_y = 185 + i * 96;
      if (hit(x, y, 930, row_y + 14, 132, 42) && g_state.catalog_ready && !g_state.catalog_busy)
        return {ActionKind::catalog_install, i};
      if (hit(x, y, 1072, row_y + 14, 126, 42) && g_state.catalog_packs[i].installed && !g_state.catalog_busy) {
        if (g_catalog_remove_armed == i) {
          g_catalog_remove_armed = -1;
          return {ActionKind::catalog_remove, i};
        }
        g_catalog_remove_armed = i;
        draw_content();
        return {};
      }
    }
  } else if (g_section == Section::display_audio) {
    if (hit(x, y, 850, 205, 90, 48) || hit(x, y, 960, 205, 90, 48)) {
      const int delta = x < 950 ? -16 : 16;
      g_state.brightness = static_cast<uint8_t>(std::clamp<int>(g_state.brightness + delta, 16, 255));
      draw_content();
      return {ActionKind::brightness_changed, g_state.brightness};
    }
    if (hit(x, y, 960, 325, 160, 48)) {
      g_state.screen_timeout_sec = next_value(g_state.screen_timeout_sec, kTimeouts);
      draw_content();
      return {ActionKind::timeout_changed, g_state.screen_timeout_sec};
    }
    if (hit(x, y, 850, 445, 90, 48) || hit(x, y, 960, 445, 90, 48)) {
      const int delta = x < 950 ? -16 : 16;
      g_state.volume = static_cast<uint8_t>(std::clamp<int>(g_state.volume + delta, 0, 255));
      draw_content();
      return {ActionKind::volume_changed, g_state.volume};
    }
    if (hit(x, y, 960, 580, 160, 48)) {
      g_state.sound_default = !g_state.sound_default;
      draw_content();
      return {ActionKind::sound_changed, g_state.sound_default};
    }
    if (hit(x, y, 960, 660, 160, 48)) {
      g_state.rotation = g_state.rotation == 3 ? 1 : 3;
      return {ActionKind::rotation_changed, g_state.rotation};
    }
  } else if (g_section == Section::radio_defaults) {
    if (hit(x, y, 330, 500, 260, 50)) {
      g_state.auto_start_reception = !g_state.auto_start_reception;
      draw_content();
      return {ActionKind::auto_start_changed, g_state.auto_start_reception};
    }
    if (hit(x, y, 620, 500, 240, 50)) {
      g_state.graphics_default = !g_state.graphics_default;
      draw_content();
      return {ActionKind::graphics_changed, g_state.graphics_default};
    }
  } else if (g_section == Section::companion) {
    if (hit(x, y, 960, 150, 170, 48))
      return {ActionKind::web_console_changed, g_state.web_console_enabled ? 0 : 1};
  }
  return {};
}

bool active() { return g_active; }
const State& state() { return g_state; }
Section section() { return g_section; }

void show_documentation_section(Section section, const State& state_value,
                                bool show_wifi_keyboard) {
  if (section >= Section::count) return;
  g_state = state_value;
  g_section = section;
  g_edit = EditField::none;
  g_wifi_edit = WifiEdit::none;
  g_active = true;
  if (show_wifi_keyboard) {
    begin_wifi_edit(WifiEdit::password, "Demo Network");
    strlcpy(g_wifi_edit_password, "example-password",
            sizeof(g_wifi_edit_password));
  }
  draw();
  if (show_wifi_keyboard) draw_wifi_keyboard();
}

bool take_wifi_credentials(char* ssid, size_t ssid_size,
                           char* password, size_t password_size) {
  if (!g_wifi_request_pending || !ssid || !password || ssid_size == 0 ||
      password_size == 0) return false;
  strlcpy(ssid, g_wifi_request_ssid, ssid_size);
  strlcpy(password, g_wifi_request_password, password_size);
  memset(g_wifi_request_password, 0, sizeof(g_wifi_request_password));
  g_wifi_request_ssid[0] = '\0';
  g_wifi_request_pending = false;
  return true;
}

bool self_check() {
  if (!valid_coordinate(EditField::latitude, 90.0) ||
      valid_coordinate(EditField::latitude, 90.00001) ||
      !valid_coordinate(EditField::longitude, -180.0) ||
      valid_coordinate(EditField::longitude, -180.00001)) return false;
  if (next_value<uint16_t>(100, kRanges) != 10 ||
      next_value<uint16_t>(0, kTimeouts) != 30) return false;
  if (strlen(wifi_key_row(0)) != 10 || strlen(wifi_key_row(2)) != 9) return false;
  const bool saved_symbols = g_wifi_symbols;
  g_wifi_symbols = true;
  const bool symbols_ok = strchr(wifi_key_row(1), '\\') &&
                          strchr(wifi_key_row(2), '~');
  g_wifi_symbols = saved_symbols;
  char ssid[33]{}, password[64]{};
  strlcpy(g_wifi_request_ssid, "SELF-CHECK", sizeof(g_wifi_request_ssid));
  strlcpy(g_wifi_request_password, "not-a-real-password",
          sizeof(g_wifi_request_password));
  g_wifi_request_pending = true;
  const bool handoff_ok = take_wifi_credentials(
      ssid, sizeof(ssid), password, sizeof(password));
  const bool credentials_ok = handoff_ok && strcmp(ssid, "SELF-CHECK") == 0 &&
                              strcmp(password, "not-a-real-password") == 0 &&
                              g_wifi_request_password[0] == '\0';
  memset(password, 0, sizeof(password));
  if (!symbols_ok || !credentials_ok) return false;
  return true;
}

}  // namespace orcsdr::settings

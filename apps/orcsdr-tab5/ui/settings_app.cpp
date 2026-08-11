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
constexpr uint16_t kRanges[] = {10, 25, 50, 100};
constexpr uint16_t kTimeouts[] = {0, 30, 60, 120, 300};
constexpr const char* kLabels[] = {
    "CONNECTIVITY", "LOCATION & ADS-B", "DATA & MAPS", "DISPLAY & AUDIO",
    "RADIO DEFAULTS", "STORAGE", "COMPANION", "SYSTEM"};

static_assert(static_cast<uint8_t>(Section::count) == std::size(kLabels));
static_assert(std::size(kRanges) == 4 && kRanges[0] == 10 && kRanges[3] == 100);

enum class EditField : uint8_t { none, latitude, longitude };

EXT_RAM_BSS_ATTR State g_state;
Section g_section = Section::connectivity;
EditField g_edit = EditField::none;
bool g_active = false;
bool g_latitude_set = false;
bool g_longitude_set = false;
char g_entry[20]{};

bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color, uint8_t size = 2,
          textdatum_t datum = middle_left) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
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
    strlcpy(status, g_state.wifi_scanning ? "Wi-Fi scanning" : "Wi-Fi offline",
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
  char value[64];
  value_row("STATUS", g_state.wifi_connected ? "CONNECTED" : "OFFLINE", 165,
            g_state.wifi_connected ? kGreen : TFT_ORANGE);
  value_row("SSID", g_state.wifi_connected ? g_state.wifi_ssid : "--", 215);
  value_row("IP ADDRESS", g_state.wifi_connected ? g_state.wifi_ip : "--", 265);
  snprintf(value, sizeof(value), g_state.wifi_connected ? "%d dBm" : "--",
           g_state.wifi_rssi);
  value_row("SIGNAL", value, 315);
  snprintf(value, sizeof(value), "%u / 4", g_state.saved_network_count);
  value_row("SAVED NETWORKS", value, 365);
  value_row("WI-FI ANTENNA", "BOARD DEFAULT (READ ONLY)", 415, kMuted);
  button(g_state.wifi_scanning ? "SCANNING..." : "SCAN NETWORKS", 330, 470, 260,
         52, g_state.wifi_scanning ? TFT_DARKGREY : TFT_DARKCYAN);
  int y = 548;
  for (uint8_t i = 0; i < g_state.network_count && i < 6; ++i) {
    snprintf(value, sizeof(value), "%s  %d dBm%s", g_state.networks[i].ssid,
             g_state.networks[i].rssi, g_state.networks[i].secure ? "  LOCK" : "");
    text(value, 340 + (i % 2) * 440, y + (i / 2) * 42, TFT_LIGHTGREY, 2);
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
  value_row("AIRCRAFT RUNTIME INDEX", g_state.sd_ready ? "SD CARD" : "UNAVAILABLE", 175);
  value_row("FAA SOURCE ARCHIVE", g_state.sd_ready ? "SD CARD" : "UNAVAILABLE", 225);
  value_row("LOCATION INDEX", "NOT INSTALLED", 275);
  value_row("MAP PACK", g_state.map_pack[0] ? g_state.map_pack : "NOT INSTALLED", 325);
  value_row("AUTOMATIC UPDATES", "OFF", 375, kMuted);
  text("Updates and map installs will require reception pause confirmation.",
       330, 465, TFT_LIGHTGREY, 2);
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
  value_row("PHONE CONNECTION", "OPTIONAL", 175, kGreen);
  value_row("PAIRED PHONES", "0 / 4", 225);
  value_row("LOCAL DISCOVERY", "NOT ENABLED", 275, kMuted);
  value_row("BLUETOOTH", g_state.companion_supported ? "AVAILABLE" : "FEASIBILITY PENDING",
            325, kMuted);
  text("OrcSDR remains fully usable with no phone, BLE, GPS, or HIVE.", 330, 430,
       TFT_LIGHTGREY, 2);
}

void draw_system() {
  text("SYSTEM", 330, 115, kBlue, 3);
  char value[40];
  value_row("BUILD", g_state.build_identity, 175);
  snprintf(value, sizeof(value), "%lu SEC", static_cast<unsigned long>(g_state.uptime_seconds));
  value_row("UPTIME", value, 225);
  value_row("NETWORK", g_state.wifi_connected ? "CONNECTED" : "OFFLINE", 275);
  value_row("SD", g_state.sd_ready ? "READY" : "UNAVAILABLE", 325);
  value_row("USB / DECODER", "SEE SERIAL DIAGNOSTICS", 375);
  value_row("M5LAUNCHER", "COMPATIBILITY INFO ONLY", 425, kMuted);
  text("Reboot, reset, export, and Launcher handoff require separate safety gates.",
       330, 515, TFT_LIGHTGREY, 2);
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
  g_active = true;
  g_latitude_set = state_value.location_configured;
  g_longitude_set = state_value.location_configured;
  draw();
}

void draw() {
  M5.Display.fillScreen(kBg);
  draw_header();
  draw_rail();
  draw_content();
}

void update(const State& state_value) {
  const bool header_changed = g_state.wifi_connected != state_value.wifi_connected ||
                              g_state.wifi_scanning != state_value.wifi_scanning ||
                              strcmp(g_state.wifi_ssid, state_value.wifi_ssid) != 0 ||
                              strcmp(g_state.wifi_ip, state_value.wifi_ip) != 0;
  const bool page_changed = g_section == Section::connectivity &&
                            (g_state.wifi_scanning != state_value.wifi_scanning ||
                             g_state.network_count != state_value.network_count ||
                             g_state.wifi_rssi != state_value.wifi_rssi);
  g_state = state_value;
  if (header_changed) draw_header();
  if (page_changed && g_edit == EditField::none) draw_content();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
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
  if (g_section == Section::connectivity && hit(x, y, 330, 470, 260, 52) &&
      !g_state.wifi_scanning) {
    return {ActionKind::scan_wifi, 0};
  }
  if (g_section == Section::location_adsb) {
    if (hit(x, y, 820, 204, 398, 54)) { start_edit(EditField::latitude); draw_keypad(); }
    else if (hit(x, y, 820, 274, 398, 54)) { start_edit(EditField::longitude); draw_keypad(); }
    else if (hit(x, y, 820, 344, 398, 54)) {
      g_state.radar_range_nm = next_value(g_state.radar_range_nm, kRanges);
      draw_content();
      return {ActionKind::range_changed, g_state.radar_range_nm};
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
  }
  return {};
}

bool active() { return g_active; }
const State& state() { return g_state; }

bool self_check() {
  if (!valid_coordinate(EditField::latitude, 90.0) ||
      valid_coordinate(EditField::latitude, 90.00001) ||
      !valid_coordinate(EditField::longitude, -180.0) ||
      valid_coordinate(EditField::longitude, -180.00001)) return false;
  if (next_value<uint16_t>(100, kRanges) != 10 ||
      next_value<uint16_t>(0, kTimeouts) != 30) return false;
  return true;
}

}  // namespace orcsdr::settings

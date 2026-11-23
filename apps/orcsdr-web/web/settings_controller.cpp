// SETTINGS: OrcSDR's device settings screen, showing the node it is talking to. The Tab5 settings (Wi-Fi, location, display and audio,
// radio defaults, storage, system and clock) become the node's: its Wi-Fi link, where this browser says it is (shared with ADS-B and
// the airband screen), the node's SD card, firmware and clock. What cannot be changed from a page is said so rather than pretended:
// the node's Wi-Fi credentials are set from its serial console (an unauthenticated HTTP route that accepted a network password would
// be a hole), and the Tab5's panel brightness, rotation, battery and C6 radio firmware do not exist here.
#include <M5Unified.h>
#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "clock_settings.hpp"
#include "data_screens.hpp"
#include "settings_app.hpp"

namespace {

using namespace orcsdr;
using namespace dscreens;
namespace st = orcsdr::settings;

st::State g_state;
bool g_entered = false;
char g_ring[6][220];
size_t g_read = 0, g_write = 0;
double g_page_ms = 0;
uint32_t g_page_utc = 0;
int g_offset_min = 0;
bool g_have_report = false;
uint32_t g_last_push = 0;

void push(const char* text) {
  if ((g_write + 1) % 6 == g_read) return;
  snprintf(g_ring[g_write], sizeof g_ring[0], "%s", text);
  g_write = (g_write + 1) % 6;
}

void note(const char* message) {
  char text[220];
  snprintf(text, sizeof text, "{\"op\":\"toast\",\"text\":\"%s\"}", message);
  push(text);
}

void fill_clock() {
  g_state.utc_offset_minutes = (int16_t)g_offset_min;
  if (!g_state.rtc_valid) return;
  const time_t t = (time_t)g_state.rtc_epoch;
  struct tm utc;
  gmtime_r(&t, &utc);
  snprintf(g_state.rtc_utc, sizeof g_state.rtc_utc, "%04d-%02d-%02d %02d:%02d:%02d", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
           utc.tm_min, utc.tm_sec);
  clock_settings::format_local(g_state.rtc_local, sizeof g_state.rtc_local, g_state.rtc_epoch, g_offset_min);
}

void enter(uint32_t) {
  g_state = st::State{};
  g_state.wifi_power_enabled = true;
  snprintf(g_state.wifi_message, sizeof g_state.wifi_message, "ASKING THE NODE...");
  snprintf(g_state.default_band, sizeof g_state.default_band, "FM");
  snprintf(g_state.build_identity, sizeof g_state.build_identity, "OrcSDR Web");
  g_state.companion_supported = false;
  g_state.web_console_enabled = true;
  g_state.web_console_listening = true;
  snprintf(g_state.web_console_url, sizeof g_state.web_console_url, "this page");
  g_state.battery_level = -1;
  g_have_report = false;
  M5.Display.fillScreen(0);
  st::enter(g_state, st::Section::connectivity);
  g_entered = true;
  push("{\"op\":\"wifi_scan\"}");
}

void leave() {
  if (!g_entered) return;
  st::leave();
  g_entered = false;
}

void update(uint32_t now) {
  if (!g_entered) return;
  if (g_state.rtc_valid && g_page_ms > 0) {
    // the clock ticks between reports
  }
  if (now - g_last_push >= 250) {
    g_last_push = now;
    fill_clock();
    st::update(g_state);
  }
}

void apply_state_actions(const st::Action& a) {
  const st::State& live = st::state();   // the screen edits its own copy; the action says which part changed
  char text[220];
  switch (a.kind) {
    case st::ActionKind::scan_wifi:
      push("{\"op\":\"wifi_scan\"}");
      break;
    case st::ActionKind::connect_wifi:
    case st::ActionKind::connect_saved_wifi:
    case st::ActionKind::forget_wifi:
    case st::ActionKind::move_wifi_up:
    case st::ActionKind::move_wifi_down:
      note("The node's Wi-Fi is set from its serial console (docs/CLI.md); a page cannot send it a password.");
      break;
    case st::ActionKind::wifi_power_changed:
    case st::ActionKind::wifi_start_at_boot_changed:
    case st::ActionKind::wifi_antenna_changed:
      note("The node keeps its own radio settings.");
      break;
    case st::ActionKind::location_changed:
    case st::ActionKind::range_changed:
      snprintf(text, sizeof text, "{\"op\":\"setting\",\"key\":\"adsb\",\"range\":%u,\"loc\":%s,\"lat_e7\":%d,\"lon_e7\":%d}", (unsigned)live.radar_range_nm,
               live.latitude_e7 || live.longitude_e7 ? "true" : "false", (int)live.latitude_e7, (int)live.longitude_e7);
      push(text);
      g_state.latitude_e7 = live.latitude_e7;
      g_state.longitude_e7 = live.longitude_e7;
      g_state.radar_range_nm = live.radar_range_nm;
      g_state.location_configured = live.latitude_e7 || live.longitude_e7;
      snprintf(g_state.location_label, sizeof g_state.location_label, "SET IN THIS BROWSER");
      break;
    case st::ActionKind::location_ip_lookup:
    case st::ActionKind::location_query_lookup:
    case st::ActionKind::location_ip_confirm:
      note("Use the Location button under the page for the browser's own position, or enter coordinates here.");
      break;
    case st::ActionKind::clock_set_utc:
      snprintf(text, sizeof text, "{\"op\":\"settime\",\"epoch\":%u,\"offset\":%d}", (unsigned)a.utc, (int)a.value);
      push(text);
      g_offset_min = (int)a.value;
      g_state.rtc_epoch = a.utc;
      g_state.rtc_valid = true;
      break;
    case st::ActionKind::clock_offset_changed:
      snprintf(text, sizeof text, "{\"op\":\"setoffset\",\"offset\":%d}", (int)a.value);
      push(text);
      g_offset_min = (int)a.value;
      break;
    case st::ActionKind::clock_ntp_sync:
      push("{\"op\":\"sync_clock\"}");   // there is no time server to ask: this browser's clock is the source
      break;
    case st::ActionKind::brightness_changed:
    case st::ActionKind::rotation_changed:
    case st::ActionKind::timeout_changed:
      note("The page is the display: brightness, rotation and timeout are the browser's.");
      break;
    default: break;
  }
}

Result touch(int x, int y, bool pressed, uint32_t) {
  Result r;
  if (!g_entered || !pressed) return r;
  const st::Action a = st::handle_touch(x, y);
  if (a.kind == st::ActionKind::close) { r.exit = true; return r; }
  if (a.kind != st::ActionKind::none) apply_state_actions(a);
  return r;
}

// The page's report: {"type":"settings","node":"..","firmware":"..","uptime_s":N,"wifi":{..},"sd":{..},"time":{..},"loc":{..},"scan":{...}}
void event(const char* line, uint32_t now) {
  char v[64];
  g_have_report = true;
  str(line, "firmware", v, sizeof v);
  char node[40];
  str(line, "node", node, sizeof node);
  snprintf(g_state.build_identity, sizeof g_state.build_identity, "%s %s", node, v);
  g_state.uptime_seconds = (uint32_t)num(line, "uptime_s");
  g_state.wifi_ready = flag(line, "started");
  g_state.wifi_connected = flag(line, "connected");
  str(line, "ssid", g_state.wifi_ssid, sizeof g_state.wifi_ssid);
  str(line, "ip", g_state.wifi_ip, sizeof g_state.wifi_ip);
  g_state.wifi_rssi = (int16_t)num(line, "rssi");
  g_state.wifi_connecting = g_state.wifi_ready && !g_state.wifi_connected && g_state.wifi_ssid[0];
  snprintf(g_state.wifi_message, sizeof g_state.wifi_message, "%s",
           !g_state.wifi_ready ? "THE NODE'S WI-FI RADIO IS NOT STARTED" : g_state.wifi_connected ? "CONNECTED" : g_state.wifi_ssid[0] ? "NOT CONNECTED" : "NO NETWORK SAVED");
  g_state.sd_ready = flag(line, "mounted");
  g_state.sd_free_bytes = (uint64_t)num(line, "free_bytes");
  g_state.sd_total_bytes = (uint64_t)num(line, "capacity_bytes");
  g_state.rtc_valid = flag(line, "synced");
  g_state.rtc_epoch = (uint32_t)num(line, "epoch");
  g_state.ntp_state = 0;
  g_offset_min = (int)num(line, "offset_min");
  if (flag(line, "loc_set")) {
    g_state.location_configured = true;
    g_state.latitude_e7 = (int32_t)num(line, "lat_e7");
    g_state.longitude_e7 = (int32_t)num(line, "lon_e7");
    snprintf(g_state.location_label, sizeof g_state.location_label, "SET IN THIS BROWSER");
  }
  g_state.radar_range_nm = (uint16_t)std::max(5.0, num(line, "range_nm", 25));
  // networks the node's radio heard (its last scan)
  uint8_t n = 0;
  const char* p = line;
  const char* aps = strstr(line, "\"aps\"");
  if (aps) p = aps;
  while (aps && n < 6 && (p = strstr(p, "{\"ssid\""))) {
    const char* end = strchr(p, '}');
    if (!end) break;
    char one[260];
    const size_t len = std::min<size_t>(sizeof one - 1, (size_t)(end - p) + 1);
    memcpy(one, p, len);
    one[len] = 0;
    st::WifiNetwork& w = g_state.networks[n];
    w = st::WifiNetwork{};
    str(one, "ssid", w.ssid, sizeof w.ssid);
    w.rssi = (int16_t)num(one, "rssi");
    w.secure = flag(one, "secure");
    w.saved = g_state.wifi_ssid[0] && !strcmp(w.ssid, g_state.wifi_ssid);
    ++n;
    p = end + 1;
  }
  g_state.network_count = n;
  g_state.wifi_scanning = num(line, "scan_state") == 1;
  g_state.saved_network_count = g_state.wifi_ssid[0] ? 1 : 0;
  if (g_state.saved_network_count) {
    snprintf(g_state.profiles[0].ssid, sizeof g_state.profiles[0].ssid, "%s", g_state.wifi_ssid);
    g_state.profiles[0].connected = g_state.wifi_connected;
  }
  g_page_utc = g_state.rtc_epoch;
  g_page_ms = now;
  fill_clock();
}

void set_num(const char*, double) {}

const char* take_request() {
  if (g_read == g_write) return nullptr;
  const char* text = g_ring[g_read];
  g_read = (g_read + 1) % 6;
  return text;
}

}  // namespace

namespace dscreens {
extern const Screen kSettings = {dashboards::Id::settings, "settings", enter, leave, update, touch, event, set_num, take_request, nullptr, nullptr, nullptr, nullptr};
}

// 2.4 GHz screen: the part of OrcSDR's main.cpp that owns the Wi-Fi survey (the access-point list and the scan state), fed by the node's
// own radio. The node scans in the background without dropping its connection (POST /api/v1/wifi/scan) and the page hands the result
// over as one JSON document, so what the screen shows is what the node's Wi-Fi radio heard, not anything this browser can see.
#include <M5Unified.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "data_screens.hpp"
#include "rf24_dashboard.hpp"

namespace {

using namespace orcsdr;
using namespace dscreens;

rf24::Snapshot g_snap;
bool g_entered = false, g_node_ready = false;
bool g_request_pending = false;
char g_request[48];
double g_age_at_ms = 0;
uint32_t g_age_ms_at_poll = 0;

void ask_scan() {
  snprintf(g_request, sizeof g_request, "{\"op\":\"wifi_scan\"}");
  g_request_pending = true;
}

void refresh_age(uint32_t now) {
  if (!g_snap.scan_complete) return;
  const uint32_t age = g_age_ms_at_poll + (uint32_t)std::max(0.0, (double)now - g_age_at_ms);
  g_snap.scan_age_seconds = (uint16_t)std::min<uint32_t>(65535u, age / 1000u);
}

void enter(uint32_t now) {
  g_snap.sound_enabled = false;
  g_snap.visualizer_available = true;
  rf24::enter(g_snap);
  g_entered = true;
  if (!g_snap.scan_complete) ask_scan();
  (void)now;
}

void leave() {
  if (!g_entered) return;
  rf24::leave();
  g_entered = false;
}

void update(uint32_t now) {
  if (!g_entered) return;
  refresh_age(now);
  rf24::update(g_snap);
}

Result touch(int x, int y, bool pressed, uint32_t) {
  Result r;
  if (!pressed || !g_entered) return r;
  const rf24::Action a = rf24::handle_touch(x, y, g_snap);
  switch (a.kind) {
    case rf24::ActionKind::close: r.exit = true; break;
    case rf24::ActionKind::rescan: if (!g_snap.scanning) ask_scan(); break;
    case rf24::ActionKind::toggle_mute: r.action = "sound_toggle"; break;
    case rf24::ActionKind::open_settings: r.action = "open_device_settings"; break;
    default: break;
  }
  return r;
}

// The node's scan document: {"state":0|1|2,"started":bool,"count":N,"total":N,"age_ms":N,"duration_ms":N,"aps":[{...},...]}
void event(const char* line, uint32_t now) {
  const int state = (int)num(line, "state");
  g_node_ready = flag(line, "started");
  rf24::Snapshot next = g_snap;
  next.ready = g_node_ready;
  next.scanning = state == 1;
  next.scan_complete = state == 2 || (state == 1 && g_snap.scan_complete);
  next.total_access_point_count = (uint16_t)std::min(65535.0, num(line, "total"));
  next.scan_duration_ms = (uint16_t)std::min(65535.0, num(line, "duration_ms"));
  g_age_ms_at_poll = (uint32_t)num(line, "age_ms");
  g_age_at_ms = now;
  snprintf(next.message, sizeof next.message, "%s", state == 1 ? "SCANNING" : state == 2 ? "SCAN COMPLETE" : g_node_ready ? "NO SCAN YET" : "WI-FI RADIO NOT STARTED");
  uint8_t n = 0;
  rf24::AccessPoint aps[rf24::kAccessPointCapacity];
  const char* p = line;
  while (n < rf24::kAccessPointCapacity && (p = strstr(p, "{\"ssid\""))) {
    const char* end = strstr(p, "\"secure\":");
    if (!end) break;
    end = strchr(end, '}');
    if (!end) break;
    char one[300];
    const size_t len = std::min<size_t>(sizeof one - 1, (size_t)(end - p) + 1);
    memcpy(one, p, len);
    one[len] = 0;
    rf24::AccessPoint& a = aps[n];
    a = rf24::AccessPoint{};
    str(one, "ssid", a.ssid, sizeof a.ssid);
    char bssid[16];
    str(one, "bssid", bssid, sizeof bssid);
    for (int i = 0; i < 6 && strlen(bssid) >= (size_t)(i * 2 + 2); ++i) {
      char hex[3] = {bssid[i * 2], bssid[i * 2 + 1], 0};
      a.bssid[i] = (uint8_t)strtol(hex, nullptr, 16);
    }
    a.rssi = (int16_t)num(one, "rssi");
    a.channel = (uint8_t)num(one, "channel");
    a.secondary_channel_offset = (int8_t)num(one, "second");
    str(one, "security", a.security, sizeof a.security);
    str(one, "phy", a.phy, sizeof a.phy);
    a.secure = flag(one, "secure");
    ++n;
    p = end + 1;
  }
  std::sort(aps, aps + n, [](const rf24::AccessPoint& l, const rf24::AccessPoint& r) { return l.rssi > r.rssi; });
  bool same = n == next.access_point_count;
  for (uint8_t i = 0; same && i < n; ++i) same = !memcmp(&aps[i], &next.access_points[i], sizeof(rf24::AccessPoint));
  if (!same || state != (g_snap.scanning ? 1 : g_snap.scan_complete ? 2 : 0)) ++next.revision;
  next.access_point_count = n;
  for (uint8_t i = 0; i < n; ++i) next.access_points[i] = aps[i];
  g_snap = next;
  refresh_age(now);
}

void set_num(const char* key, double v) {
  if (!strcmp(key, "sound")) g_snap.sound_enabled = v != 0;
}

const char* take_request() {
  if (!g_request_pending) return nullptr;
  g_request_pending = false;
  return g_request;
}

}  // namespace

namespace dscreens {
extern const Screen kWifi = {dashboards::Id::wifi_analysis, "wifi", enter, leave, update, touch, event, set_num, take_request, nullptr, nullptr, nullptr};
}

// 2.4 GHz WIFI analysis (apps/orcsdr-tab5/ui/rf24_dashboard.cpp, unchanged) on the P4 console: an access-point
// survey from the ESP32-C6's scan results.
#include <algorithm>
#include <cstring>
#include "esp_timer.h"
#include "home_app.hpp"
#include "rf24_dashboard.hpp"
#include "screens.hpp"
#include "wifi_app.hpp"

namespace {
namespace rf = orcsdr::rf24;
int64_t g_last_scan_us = 0;
int64_t g_scan_requested_us = 0;
uint32_t g_revision = 1;
bool g_seen_scanning = false;

const rf::Snapshot& build() {
  static rf::Snapshot s;
  const wifi_app::Status w = wifi_app::status();
  orcsdr::wifi::ScanResult found[16];
  const int n = wifi_app::scan_results(found, 16);
  const bool scanning = wifi_app::scanning();
  if (g_seen_scanning && !scanning) {
    g_last_scan_us = esp_timer_get_time();
    ++g_revision;
  }
  g_seen_scanning = scanning;
  s.ready = w.started;
  s.scanning = scanning;
  s.sound_enabled = false;
  s.visualizer_available = false;
  std::snprintf(s.message, sizeof(s.message), "%s", w.started ? (scanning ? "Scanning..." : "Scan complete") : "Wi-Fi co-processor starting");
  s.access_point_count = static_cast<uint8_t>(std::min<int>(n, rf::kAccessPointCapacity));
  s.total_access_point_count = static_cast<uint16_t>(n);
  for (int i = 0; i < s.access_point_count; ++i) {
    rf::AccessPoint& a = s.access_points[i];
    std::snprintf(a.ssid, sizeof(a.ssid), "%.32s", found[i].ssid);
    std::memcpy(a.bssid, found[i].bssid, 6);
    a.rssi = found[i].rssi;
    a.channel = found[i].channel;
    a.secondary_channel_offset = found[i].secondary_channel_offset;
    std::snprintf(a.security, sizeof(a.security), "%.23s", found[i].security);
    std::snprintf(a.phy, sizeof(a.phy), "%.11s", found[i].phy);
    a.secure = found[i].secure;
  }
  s.scan_complete = !scanning && n > 0;
  s.scan_age_seconds = g_last_scan_us ? static_cast<uint16_t>(std::min<int64_t>(65535, (esp_timer_get_time() - g_last_scan_us) / 1000000)) : 0;
  s.revision = g_revision;
  return s;
}

void enter() {
  (void)wifi_app::request_scan();
  g_scan_requested_us = esp_timer_get_time();
  rf::enter(build());
}
void leave() { rf::leave(); }
void tick() { rf::update(build()); }

void tap(int x, int y) {
  const rf::Action a = rf::handle_touch(x, y, build());
  switch (a.kind) {
    case rf::ActionKind::close: home_app::request_home(); break;
    case rf::ActionKind::rescan: (void)wifi_app::request_scan(); break;
    case rf::ActionKind::open_settings: home_app::request_open(orcsdr::dashboards::Id::settings); break;
    default: break;  // mute / visualizer: not available on this target
  }
}
}  // namespace

namespace screens {
const Screen& wifi_screen() {
  static const Screen s{orcsdr::dashboards::Id::wifi_analysis, "WIFI_ANALYSIS", enter, leave, tick, tap, nullptr, nullptr};
  return s;
}
}  // namespace screens

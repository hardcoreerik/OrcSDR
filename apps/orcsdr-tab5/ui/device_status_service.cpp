#include "device_status_service.hpp"

#include <M5Unified.h>
#include <WiFi.h>

#include <cstring>

namespace orcsdr::device_status {

Snapshot collect(bool connected, const char* fallback_ssid, bool rtl_ready,
                 const char* rtl_status) {
  Snapshot snapshot{};
  snapshot.wifi_connected = connected;
  snapshot.rtl_ready = rtl_ready;
  snapshot.battery_percent = M5.Power.getBatteryLevel();
  snapshot.battery_mv = M5.Power.getBatteryVoltage();
  snapshot.battery_current_ma = M5.Power.getBatteryCurrent();
  snapshot.vbus_mv = M5.Power.getVBUSVoltage();
  strlcpy(snapshot.rtl_status, rtl_status ? rtl_status : "", sizeof(snapshot.rtl_status));
  if (connected) {
    strlcpy(snapshot.wifi_ssid, WiFi.SSID().c_str(), sizeof(snapshot.wifi_ssid));
    const String ip = WiFi.localIP().toString();
    strlcpy(snapshot.wifi_ip, ip.c_str(), sizeof(snapshot.wifi_ip));
    snapshot.wifi_rssi = WiFi.RSSI();
  } else {
    strlcpy(snapshot.wifi_ssid, fallback_ssid ? fallback_ssid : "", sizeof(snapshot.wifi_ssid));
  }
  return snapshot;
}

bool self_check() {
  Snapshot snapshot{};
  strlcpy(snapshot.wifi_ssid, "test", sizeof(snapshot.wifi_ssid));
  strlcpy(snapshot.rtl_status, "ready", sizeof(snapshot.rtl_status));
  return strcmp(snapshot.wifi_ssid, "test") == 0 && strcmp(snapshot.rtl_status, "ready") == 0;
}

}  // namespace orcsdr::device_status

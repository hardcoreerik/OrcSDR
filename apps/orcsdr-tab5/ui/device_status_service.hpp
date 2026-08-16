#pragma once

#include <cstdint>

namespace orcsdr::device_status {

struct Snapshot {
  bool wifi_connected = false;
  bool rtl_ready = false;
  int16_t wifi_rssi = 0;
  int32_t battery_percent = -1;
  int16_t battery_mv = -1;
  int32_t battery_current_ma = 0;
  int16_t vbus_mv = -1;
  char wifi_ssid[33]{};
  char wifi_ip[16]{};
  char rtl_status[96]{};
};

Snapshot collect(bool wifi_connected, const char* fallback_ssid, bool rtl_ready,
                 const char* rtl_status);
bool self_check();

}  // namespace orcsdr::device_status

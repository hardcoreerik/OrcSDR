#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::wifi {

struct ScanResult {
  char ssid[33]{};
  uint8_t bssid[6]{};
  int16_t rssi = 0;
  uint8_t channel = 0;
  int8_t secondary_channel_offset = 0;
  char security[24]{};
  char phy[12]{};
  bool secure = false;
};

bool start();
void stop();
bool begin_scan();
int scan_results(ScanResult* results, size_t capacity);
bool connect(const char* ssid, const char* password);
void disconnect();
bool connected();
bool connect_failed();
const char* ssid();
const char* ip();
int16_t rssi();
bool hosted_versions_match();
const char* hosted_c6_version();
bool hosted_transport_ready();
const char* hosted_failure_stage();
int32_t hosted_failure_code();

}  // namespace orcsdr::wifi

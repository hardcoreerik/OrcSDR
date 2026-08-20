#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::wifi {

struct ScanResult {
  char ssid[33]{};
  int16_t rssi = 0;
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
const char* hosted_failure_stage();
int32_t hosted_failure_code();

}  // namespace orcsdr::wifi

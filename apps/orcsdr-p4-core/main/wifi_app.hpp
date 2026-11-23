#pragma once
// Wi-Fi on the Waveshare board: the ESP32-C6 co-processor over SDIO (ESP-Hosted), driven by the Tab5's
// wifi_service module (unchanged). This file owns the boot-time start, saved credentials and a thread-safe
// view for Settings and the serial interpreter. Passwords are stored in NVS and never printed.
#include <cstddef>
#include <cstdint>
#include "wifi_service.hpp"

namespace wifi_app {
void start();  // background task: brings the C6 link up a few seconds after boot, then joins the saved network

struct Status {
  bool started = false;         // the C6 link and Wi-Fi driver are up
  bool link_failed = false;
  bool connected = false;
  bool connecting = false;
  char ssid[33] = {};
  char ip[16] = {};
  int16_t rssi = 0;
  char c6_version[16] = {};
  char failure_stage[24] = {};
  int failure_code = 0;
  bool power = true;            // Wi-Fi allowed (Settings > Connectivity)
  bool start_at_boot = true;    // join the saved network automatically
  bool saved = false;           // credentials are stored
  char saved_ssid[33] = {};
};
Status status();

bool request_scan();  // asynchronous; results via scan_results()
int scan_results(orcsdr::wifi::ScanResult* out, size_t capacity);
bool scanning();
bool connect(const char* ssid, const char* password, bool remember);
void disconnect();
void simulate_link_loss();  // test only: behaves as if the C6 link broke
void set_power(bool enabled);          // off: disconnects and stops Wi-Fi; on: brings the C6 link up if needed
void set_start_at_boot(bool enabled);  // whether the saved network is joined automatically
void forget();
}  // namespace wifi_app

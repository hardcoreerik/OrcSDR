#pragma once
// OrcSDR's use of esp-rtl-tcp (components/esp_rtl_tcp): adapts the receiver service to the server's Host interface and registers the
// IQ feed as a queued sink while a client is connected. Off by default. Design: docs/hardware/ESP_RTL_TCP_DESIGN.md.
#include "esp_rtl_tcp.hpp"

namespace rtltcp_app {

struct Status {
  bool enabled = false;      // saved setting
  bool control = false;      // saved setting: false = listen-only
  int receiver = 0;
  uint16_t port = 1234;
  esp_rtl_tcp::Stats server;
  uint32_t sink_blocks_dropped = 0;  // whole blocks the receiver service dropped for this client (slow link), over all sessions since boot
};

void start();                      // restores the saved settings and starts a task that serves while enabled and a network address exists
void set_enabled(bool on);
void set_control(bool on);         // the server restarts so the mode applies to a fresh session
Status status();

}  // namespace rtltcp_app

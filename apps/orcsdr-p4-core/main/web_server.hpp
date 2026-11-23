#pragma once
// Read-only web layer (docs/hardware/WEB_LAYER_DESIGN.md, first slice). An HTTP server on port 80 that serves the shared
// command table as JSON under /api/v1/<path>. Off by default; it runs only while enabled and Wi-Fi is connected.
// Read tier only: no route here can change the radio, the network credentials or the device.
#include <cstdint>
#include "esp_http_server.h"

namespace web_server {
void start();               // loads the saved on/off choice and starts the manager task (does not open a port by itself)
void set_enabled(bool on);  // persists the choice; the manager starts or stops the server within about a second
bool enabled();
bool listening();
const char* url();          // "http://<ip>/" while listening, otherwise "offline"

// For the port-81 WebSocket server (web_iq): the same Host allow-list and same-site check as port 80 (the Origin names the same host; the port may differ),
// and the client's address.
bool same_site_request(httpd_req_t* req);
uint32_t peer_address(httpd_req_t* req);
}  // namespace web_server

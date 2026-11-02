#pragma once
// Channel IQ for the OrcSDR Web page (docs/hardware/WEB_LAYER_DESIGN.md section 17). The page asks for a "channel" (POST /api/v1/channel, signed),
// then opens a WebSocket to port 81 /api/v1/fm/iq and demodulates AM, NFM and SSB in the browser from a ~300 kS/s window.
// The node parks the receiver on a window around the wanted frequency, decimates its stream with a CIC filter and sends it as 8-bit IQ packets.
// Built only when the HTTP server has WebSocket support and lwip has 16 sockets (sdkconfig.net-throughput); otherwise every call says not_available.
#include <cstddef>
#include <cstdint>
#include "esp_http_server.h"

namespace web_iq {
enum class Start : uint8_t { ok, bad_request, not_available };

// Builds the JSON reply for POST channel. `peer` is the signed caller's address: only that address may open the WebSocket, within 30 s.
Start start_channel(int rx, uint32_t hz, uint32_t peer, char* out, size_t cap);
void stop_channel();  // ends the stream and closes the WebSocket
void shutdown();      // also stops the port-81 server (the web server stopped)
bool available();     // compiled in
}  // namespace web_iq

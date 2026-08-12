#pragma once

#include <cstdint>

namespace orcsdr::web {

/** Start Ethernet (Waveshare IP101 defaults) and HTTP server on port 80. */
bool begin_network_and_http();

/** Poll DHCP/events and HTTP clients — call from loop(). */
void poll_network_and_http();

/** Current DHCP address or "0.0.0.0". */
const char* local_ip();

bool ethernet_link_up();
bool http_ready();

/** Non-zero once: 1=ADSB, 2=FM, 3=WX. Cleared on read. */
uint8_t take_mode_request();

/** Non-zero frequency request in Hz; 0 if none. Cleared on read. */
uint32_t take_freq_request();

/** Non-zero once: 1=start stream, 2=stop stream. Cleared on read. */
uint8_t take_stream_request();

}  // namespace orcsdr::web

#pragma once

// Which receiver source OrcSDR should use: the local USB dongle through
// esp-rtl-sdr, or a remote rtl_tcp server. Pure decision logic with no
// hardware or network access, so every case is host-tested.

#include <cstdint>

namespace orcsdr::source {

enum class Mode : uint8_t { automatic, local_usb, remote_tcp };
enum class Active : uint8_t { none, local_usb, remote_tcp };

struct Inputs {
  Mode mode = Mode::automatic;
  Active active = Active::none;  // source that is running or starting now
  bool usb_ready = false;        // dongle enumerated and able to stream
  bool remote_configured = false;
  bool network_up = false;       // Wi-Fi has an address
  uint32_t ms_since_boot = 0;
  uint32_t grace_ms = 8000;      // Automatic waits this long for USB first
};

// Returns the source to run. Rules:
//  - Local USB never uses the network, and Remote TCP never needs a dongle.
//  - A source that is already running is kept while it stays usable, so a
//    receiver appearing later never interrupts it.
//  - Automatic prefers USB; it falls back to the saved remote server only
//    after the grace period, and only to the server the user configured.
Active choose(const Inputs& in);

const char* mode_name(Mode mode);
bool parse_mode(const char* text, Mode* out);

}  // namespace orcsdr::source

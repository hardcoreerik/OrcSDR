#pragma once
#include <cstdint>

namespace orc {
// Read-only radio counters for the Dial Settings > LINK screen (the same numbers `ORCDIAL_RF` prints).
struct LinkDiagnostics {
  uint32_t tx_accepted = 0, tx_refused = 0, ack_ok = 0, ack_failed = 0, rx_frames = 0;
  uint32_t last_rx_age_ms = UINT32_MAX;   // UINT32_MAX: nothing heard yet
  uint8_t channel = 0;
};
} // namespace orc

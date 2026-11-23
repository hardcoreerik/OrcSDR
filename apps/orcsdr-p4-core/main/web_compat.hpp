#pragma once
// Read-only routes in the shapes the OrcSDR Web page (the browser version of the M5GFX screens) already calls:
// receivers, fm, report, tuner, spectrum and time. They translate what radio_service, wifi_app and the board profile know
// into that page's JSON, so the page runs unchanged (docs/hardware/WEB_LAYER_DESIGN.md section 10). Nothing here changes
// the device. Values the P4 console does not measure are filled with neutral defaults and listed in the design doc.
#include <cstddef>
#include <cstdint>

namespace web_compat {
enum class Kind : uint8_t { json, binary };
// If `name` is one of the legacy routes, fills `out` (at most `cap` bytes) and returns true; `rx` selects the receiver.
// A false return means "not a legacy route". `*len` is 0 when the output did not fit.
bool get(const char* name, int rx, char* out, size_t cap, size_t* len, Kind* kind);
}  // namespace web_compat

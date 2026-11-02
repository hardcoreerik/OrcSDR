#pragma once
// Control routes in the shapes the OrcSDR Web page already POSTs (receivers/tune, receivers/rate, tuner/gain, tuner/agc, fm, time, channel).
// The web server calls these only after a signed session has been verified (web_auth); nothing here checks credentials. The behaviour maps
// onto the receiver service and the Home screen the way the serial commands do. What the P4 console cannot do yet is answered honestly
// with an error body, never with a fake success (docs/hardware/WEB_LAYER_DESIGN.md section 16).
#include <cstddef>
#include <cstdint>

namespace web_control {
enum class Result : uint8_t { ok, bad_request, not_available, unknown_route };
// `route` is the part after /api/v1/ (e.g. "receivers/tune"), `query` the raw query string without the '?'. Always writes a JSON body to `out`
// (for errors too) unless the route is unknown.
Result run(const char* route, const char* query, char* out, size_t cap);
}  // namespace web_control

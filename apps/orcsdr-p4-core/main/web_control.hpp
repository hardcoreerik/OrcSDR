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
// `peer` is the signed caller's address (the channel WebSocket is opened only by that address).
Result run(const char* route, const char* query, uint32_t peer, char* out, size_t cap);

// The lowest frequency the web page may ask receiver `rx` for, from what the dongle reports: 24 kHz with a built-in upconverter (Blog V4/V4L),
// 100 kHz through Q-branch direct sampling (Blog V3/V3c, Nooelec SMArt v5), 24 MHz for a tuner-only dongle. The driver still has the last word.
uint32_t tune_min_hz(int rx);
}  // namespace web_control

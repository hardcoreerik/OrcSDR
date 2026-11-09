// Browser answers for the few device-only calls that the shared sources make and that have no meaning in a page.
#include "dead_air_env.hpp"
#include "map_download_notice.hpp"

namespace orcsdr::dead_air::env {
// A page has no Grove port, so Dead Air reports "no environment module", the same as a unit with nothing plugged in.
Bus* port_a_bus() { return nullptr; }
}  // namespace orcsdr::dead_air::env

namespace orcsdr::map_download_notice {
// Map downloads run on the device; the page never shows their notice, so there is nothing to redraw.
void invalidate() {}
}  // namespace orcsdr::map_download_notice

// Browser answers for the few device-only calls that the shared sources make and that have no meaning in a page.
#include "dead_air_env.hpp"
#include "map_download_notice.hpp"
#include "rtl_tcp_header_tag.hpp"

namespace orcsdr::dead_air::env {
// A page has no Grove port, so Dead Air reports "no environment module", the same as a unit with nothing plugged in.
Bus* port_a_bus() { return nullptr; }
}  // namespace orcsdr::dead_air::env

namespace orcsdr::map_download_notice {
// Map downloads run on the device; the page never shows their notice, so there is nothing to redraw.
void invalidate() {}
}  // namespace orcsdr::map_download_notice

namespace orcsdr::rtl_tcp_header_tag {
// The page has no rtl_tcp remote receiver, so the header carries no RTL-TCP tag and there is nothing to repaint.
void invalidate() {}
}  // namespace orcsdr::rtl_tcp_header_tag

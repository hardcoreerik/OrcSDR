#include "settings_remote_source.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "receiver_source_policy.hpp"
#include "rtl_tcp_protocol.hpp"
#include "rtl_tcp_source.hpp"

namespace orcsdr::settings_remote {

void fill(settings::State& state) {
  state.receiver_source = static_cast<uint8_t>(rtl_tcp_source::mode());
  if (rtl_tcp_source::configured()) {
    if (rtl_tcp_source::port() == rtl_tcp::kDefaultPort)
      std::snprintf(state.remote_server, sizeof(state.remote_server), "%.63s", rtl_tcp_source::host());
    else
      std::snprintf(state.remote_server, sizeof(state.remote_server), "%.63s:%u", rtl_tcp_source::host(),
                    static_cast<unsigned>(rtl_tcp_source::port()));
  } else {
    state.remote_server[0] = '\0';
  }
  const char* status = "";
  if (rtl_tcp_source::running()) {
    switch (rtl_tcp_source::state()) {
      case rtl_tcp::State::streaming: {
        const uint32_t link = rtl_tcp_source::link_ratio_pct();
        status = link > 0 && link < 85 ? "REMOTE: BEHIND REAL TIME" : "REMOTE: STREAMING";
        break;
      }
      case rtl_tcp::State::connecting: status = "REMOTE: CONNECTING"; break;
      case rtl_tcp::State::reconnecting: status = "REMOTE: RECONNECTING"; break;
      case rtl_tcp::State::failed: status = "REMOTE: FAILED"; break;
      default: break;
    }
  } else if (rtl_tcp_source::state() == rtl_tcp::State::failed) {
    status = "REMOTE: FAILED";
  }
  std::snprintf(state.remote_status, sizeof(state.remote_status), "%s", status);
}

bool handle(const settings::Action& action) {
  if (action.kind == settings::ActionKind::receiver_source_changed) {
    const int mode = action.value;
    if (mode < 0 || mode > 2) return true;
    rtl_tcp_source::set_mode(static_cast<source::Mode>(mode));  // used from the next start
    return true;
  }
  if (action.kind == settings::ActionKind::remote_server_changed) {
    char text[72]{};
    char host[rtl_tcp::kMaxHostChars + 1]{};
    uint16_t port = 0;
    if (settings::take_remote_server(text, sizeof(text)) &&
        rtl_tcp::parse_endpoint(text, host, sizeof(host), &port))
      (void)rtl_tcp_source::set_endpoint(host, port);  // refused while a stream is running
    return true;
  }
  return false;
}

}  // namespace orcsdr::settings_remote

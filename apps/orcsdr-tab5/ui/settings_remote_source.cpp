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

  // The RTL_TCP page. Live figures are rounded so the page repaints only when something a person can read has changed.
  const auto tuning = rtl_tcp_source::tuning();
  state.rt_rate_cap = tuning.rate_cap;
  state.rt_ppm_set = tuning.ppm_set;
  state.rt_ppm = static_cast<int16_t>(tuning.ppm);
  state.rt_direct_set = tuning.direct_set;
  state.rt_direct = tuning.direct_mode;
  state.rt_offset_set = tuning.offset_set;
  state.rt_offset_on = tuning.offset_on;
  const auto s = rtl_tcp_source::status();
  const char* word = "";
  if (rtl_tcp_source::running()) {
    switch (s.state) {
      case rtl_tcp::State::streaming: word = "STREAMING"; break;
      case rtl_tcp::State::connecting: word = "CONNECTING"; break;
      case rtl_tcp::State::reconnecting: word = "RETRYING"; break;
      case rtl_tcp::State::failed: word = "FAILED"; break;
      default: word = "STARTING"; break;
    }
  }
  std::snprintf(state.rt_state, sizeof(state.rt_state), "%s", word);
  const char* why = "";
  if (s.last_reason != rtl_tcp::EndReason::none && s.last_reason != rtl_tcp::EndReason::stopped &&
      (!rtl_tcp_source::streaming()))
    why = rtl_tcp::end_reason_name(s.last_reason);
  std::snprintf(state.rt_reason, sizeof(state.rt_reason), "%s", why);
  state.rt_link_pct = rtl_tcp_source::streaming() ? static_cast<uint8_t>((s.link_ratio_pct / 5) * 5 > 255 ? 255 : (s.link_ratio_pct / 5) * 5) : 0;
  state.rt_rate_sps = s.sample_rate_sps;
  if (s.tuner_type_raw != 0) std::snprintf(state.rt_tuner, sizeof(state.rt_tuner), "%s", rtl_tcp::tuner_type_name(s.tuner_type_raw));
  else state.rt_tuner[0] = '\0';
  state.rt_gain_steps = static_cast<uint16_t>(s.gain_count);
  state.rt_received_mb = static_cast<uint32_t>((s.bytes_received / 1000000ull) / 100ull * 100ull);
  const uint64_t total = s.bytes_received + s.bytes_dropped;
  state.rt_lost_permille = total ? static_cast<uint16_t>(s.bytes_dropped * 1000ull / total) : 0;
  state.rt_reconnects = static_cast<uint16_t>(s.reconnects > 65535 ? 65535 : s.reconnects);
}

bool handle(const settings::Action& action) {
  if (action.kind == settings::ActionKind::receiver_source_changed) {
    const int mode = action.value;
    if (mode < 0 || mode > 2) return true;
    rtl_tcp_source::set_mode(static_cast<source::Mode>(mode));  // used from the next start
    return true;
  }
  if (action.kind == settings::ActionKind::rtl_rate_cap_changed) {
    (void)rtl_tcp_source::set_rate_cap(static_cast<uint8_t>(action.value));   // used from the next start
    return true;
  }
  if (action.kind == settings::ActionKind::rtl_ppm_changed) {
    (void)rtl_tcp_source::set_ppm(action.value);
    return true;
  }
  if (action.kind == settings::ActionKind::rtl_direct_changed) {
    (void)rtl_tcp_source::set_direct_sampling(static_cast<uint8_t>(action.value));
    return true;
  }
  if (action.kind == settings::ActionKind::rtl_offset_changed) {
    (void)rtl_tcp_source::set_offset_tuning(action.value != 0);
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

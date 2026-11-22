#include "serial_wifi.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_wifi {

namespace {

// One formatted line, one write: what Serial.printf did for these replies.
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[512];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

void print_results(const Hooks& hooks) {
  const ScanSummary summary = hooks.scan_summary();
  say(hooks, "RTL_WIFI_RESULTS_BEGIN count=%u total=%u revision=%u age_s=%u duration_ms=%u\n", summary.count,
      summary.total, summary.revision, summary.age_s, summary.duration_ms);
  for (uint8_t i = 0; i < hooks.scan_result_count(); ++i) {
    const ScanResult ap = hooks.scan_result(i);
    say(hooks, "RTL_WIFI_AP index=%u ssid_hex=", static_cast<unsigned>(i));
    hooks.write_hex(reinterpret_cast<const uint8_t*>(ap.ssid), strlen(ap.ssid));
    hooks.write(" bssid=");
    hooks.write_hex(ap.bssid, 6);
    say(hooks, " rssi=%d channel=%u secure=%d security=%s phy=%s ht40=%d\n", ap.rssi, ap.channel, ap.secure,
        ap.security, ap.phy, ap.ht40);
  }
  hooks.write_line("RTL_WIFI_RESULTS_END");
}

void print_profiles(const Hooks& hooks) {
  say(hooks, "RTL_WIFI_PROFILES_BEGIN count=%u\n", static_cast<unsigned>(hooks.profile_count()));
  for (uint8_t i = 0; i < hooks.profile_count(); ++i) {
    say(hooks, "RTL_WIFI_PROFILE index=%u ssid_hex=", static_cast<unsigned>(i));
    const char* ssid = hooks.profile_ssid(i);
    hooks.write_hex(reinterpret_cast<const uint8_t*>(ssid), strlen(ssid));
    say(hooks, " connected=%d\n", hooks.profile_connected(i) ? 1 : 0);
  }
  hooks.write_line("RTL_WIFI_PROFILES_END");
}

void print_status(const Hooks& hooks) {
  const Status s = hooks.status();
  say(hooks,
      "RTL_WIFI_STATUS station=%d hosted_match=%d scanning=%d connecting=%d "
      "connected=%d saved_profiles=%u power=%d auto_connect=%d antenna=%s "
      "ap_count=%u rssi=%d outage_s=%lu\n",
      s.station, s.hosted_match, s.scanning, s.connecting, s.connected, s.saved_profiles, s.power, s.auto_connect,
      s.antenna, s.ap_count, s.rssi, s.outage_s);
  Link link;
  if (hooks.link(&link))
    say(hooks, "RTL_WIFI_LINK rssi=%d channel=%u second=%u b=%d g=%d n=%d ax=%d ps=%d bandwidth=%d\n", link.rssi,
        link.channel, link.second_channel, link.b, link.g, link.n, link.ax, link.ps_mode, link.bandwidth);
}

void print_channel(const Hooks& hooks) {
  const Channel c = hooks.channel();
  if (!c.ok) {
    say(hooks, "RTL_WIFI_CHANNEL_ERROR %s\n", c.error);
    return;
  }
  say(hooks, "RTL_WIFI_CHANNEL primary=%u secondary=%u connected=%d ssid_hex=", c.primary, c.secondary, c.connected);
  if (c.has_ap) hooks.write_hex(c.ssid, c.ssid_length);
  say(hooks, " ap_primary=%u\n", c.has_ap ? c.ap_primary : 0u);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_WIFI_SCAN") == 0) {
    hooks.request_scan();
    hooks.write_line("RTL_WIFI_SCAN_QUEUED");
    return true;
  }
  if (strcmp(command, "RTL_WIFI_CONNECT_SAVED") == 0 ||
      strcmp(command, "RTL_WIFI_CONNECT_SAVED PAUSE") == 0) {
    if (hooks.profile_count() == 0) {
      hooks.write_line("RTL_WIFI_CONNECT_ERROR no_saved_profile");
      return true;
    }
    hooks.connect_saved_profile();
    hooks.write_line("RTL_WIFI_CONNECT_QUEUED saved_profile=0 mode=pause");
    return true;
  }
  if (strcmp(command, "RTL_WIFI_RESULTS") == 0) {
    print_results(hooks);
    return true;
  }
  if (strcmp(command, "RTL_WIFI_PROFILES") == 0) {
    print_profiles(hooks);
    return true;
  }
  if (strncmp(command, "RTL_WIFI_OUTAGE", 15) == 0 && (command[15] == '\0' || command[15] == ' ')) {
    // Test fault injection: the access point 'vanishes' for N seconds (see wifi_service.hpp). Needs an authenticated session.
    if (!authenticated) {
      hooks.write_line("RTL_WIFI_OUTAGE_ERROR auth_required");
      return true;
    }
    const unsigned long seconds = strtoul(command + 15, nullptr, 10);
    if (seconds > 1800) {
      hooks.write_line("RTL_WIFI_OUTAGE_ERROR use 0 (cancel) to 1800 seconds");
      return true;
    }
    if (command[15] != '\0') hooks.simulate_outage(static_cast<uint32_t>(seconds));
    say(hooks, "RTL_WIFI_OUTAGE seconds_left=%lu\n", static_cast<unsigned long>(hooks.outage_remaining_s()));
    return true;
  }
  if (strcmp(command, "RTL_WIFI_DISCONNECT") == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_WIFI_DISCONNECT_ERROR auth_required");
      return true;
    }
    hooks.write_line(hooks.disconnect() ? "RTL_WIFI_DISCONNECT_OK"
                                        : "RTL_WIFI_DISCONNECT_ERROR radio_pause_failed");
    return true;
  }
#if ORCSDR_C6_FAULT_TEST
  if (strcmp(command, "RTL_WIFI_TEST_C6_POWER OFF CONFIRM") == 0 ||
      strcmp(command, "RTL_WIFI_TEST_C6_POWER ON CONFIRM") == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_WIFI_TEST_C6_POWER_ERROR auth_required");
      return true;
    }
    const bool enable = strcmp(command, "RTL_WIFI_TEST_C6_POWER ON CONFIRM") == 0;
    // Only WLAN P0 changes. USB power P3 remains untouched. This lab command
    // deliberately bypasses the normal receiver pause to exercise failure.
    const bool ok = hooks.set_c6_power(enable);
    say(hooks, "RTL_WIFI_TEST_C6_POWER_RESULT enabled=%d write_ok=%d\n", enable, ok);
    return true;
  }
#endif
  if (strcmp(command, "RTL_WIFI_BUS_STATUS") == 0) {
    // Local reads only: this remains usable when a Hosted RPC cannot reply.
    const BusStatus bus = hooks.bus_status();
    say(hooks, "RTL_WIFI_BUS_STATUS wlan_enable=%d wlan_input=%d hosted_ready=%d link_failed=%d capture_state=%u\n",
        bus.wlan_enable, bus.wlan_input, bus.hosted_ready, bus.link_failed, bus.capture_state);
    return true;
  }
  if (strcmp(command, "RTL_WIFI_STATUS") == 0) {
    print_status(hooks);
    return true;
  }
  if (strcmp(command, "RTL_WIFI_CHANNEL") == 0) {
    print_channel(hooks);
    return true;
  }
  if (strcmp(command, "RTL_WIFI_C6_STATUS") == 0) {
    const C6Status c6 = hooks.c6_status();
    say(hooks,
        "RTL_WIFI_C6_STATUS host=3.0.6 coprocessor=%s transport=%d embedded=%d "
        "state=%s percent=%u stage=%s match=%d\n",
        c6.coprocessor, c6.transport, c6.embedded, c6.state, c6.percent, c6.stage[0] ? c6.stage : "none", c6.match);
    return true;
  }
  if (strcmp(command, "RTL_WIFI_C6_UPDATE CONFIRM") == 0) {
    if (!authenticated) { hooks.write_line("RTL_WIFI_C6_UPDATE_ERROR auth_required"); return true; }
    hooks.write_line(hooks.begin_c6_update() ? "RTL_WIFI_C6_UPDATE_QUEUED"
                                              : "RTL_WIFI_C6_UPDATE_ERROR not_ready");
    return true;
  }
  if (strcmp(command, "RTL_WIFI_COEX_STATUS") == 0) {
    hooks.print_coexistence();
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_wifi

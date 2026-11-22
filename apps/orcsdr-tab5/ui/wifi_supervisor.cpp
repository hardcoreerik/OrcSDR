#include "wifi_supervisor.hpp"

#include <cstdio>

namespace orcsdr::wifi_supervisor {

uint32_t lost_recovery_delay_ms(uint8_t recoveries_so_far) {
  return recoveries_so_far == 0 ? kLostRecoveryDelayMs : kRetryEveryMs;
}

uint32_t slow_retry_delay_ms(uint8_t) { return kRetryEveryMs; }

bool service(State& state, const Vars& vars, const Hooks& hooks) {
  using orcsdr::action_io::say;
  if (!hooks.power_enabled()) {
    hooks.clear_requests();
    return false;
  }
  // Detect transport failure even when no station operation is active. Handle
  // it once before ordinary RPCs; repeated polling must not postpone recovery.
  if (hooks.link_failed() && !state.link_failure_handled) {
    state.link_failure_handled = true;
    *vars.station_ready = false;
    *vars.connected = false;
    *vars.connecting = false;
    *vars.scan_running = false;
    *vars.reconnect_due_ms = 0;
    hooks.io.write_line("RTL_WIFI_LINK_LOST");
    hooks.resume_radio_connect_pause();
    if (*vars.link_recoveries < vars.link_recovery_max) {
      hooks.set_status("Wi-Fi link lost - recovering");
      *vars.link_recovery_due_ms = hooks.now_ms() + lost_recovery_delay_ms(*vars.link_recoveries);
    } else {
      hooks.set_status("Wi-Fi link lost - restart Wi-Fi");
    }
    ++*vars.scan_revision;
    hooks.draw_state();
  }
  // Budget spent and the link still down (lost, or a recovery that did not bring the station back): schedule one slow
  // retry instead of waiting for a reboot. The recovery it runs counts as one attempt (the counter is set one short of the
  // maximum), so if it fails this schedules the next, longer pause.
  if (*vars.link_recoveries >= vars.link_recovery_max && *vars.link_recovery_due_ms == 0 &&
      (hooks.link_failed() || !*vars.station_ready)) {
    const uint32_t delay = slow_retry_delay_ms(state.slow_retries);
    if (state.slow_retries < 255) ++state.slow_retries;
    *vars.link_recoveries = static_cast<uint8_t>(vars.link_recovery_max - 1);
    *vars.link_recovery_due_ms = hooks.now_ms() + delay;
    if (*vars.link_recovery_due_ms == 0) *vars.link_recovery_due_ms = 1;   // 0 means "none due"
    char status[48];
    std::snprintf(status, sizeof(status), "Wi-Fi link lost - retrying in %lu s",
                  static_cast<unsigned long>(delay / 1000u));
    hooks.set_status(status);
    hooks.draw_state();
    say(hooks.io, "RTL_WIFI_LINK_RETRY_SCHEDULED retry=%u delay_s=%lu\n", static_cast<unsigned>(state.slow_retries),
        static_cast<unsigned long>(delay / 1000u));
  }
  // A link that has stayed up for 5 minutes earns back its recovery budget,
  // so long-running devices keep recovering while a flapping C6 cannot loop.
  if (*vars.link_recoveries && *vars.connected && *vars.connected_since_ms &&
      hooks.now_ms() - *vars.connected_since_ms >= kBudgetRefillMs) {
    *vars.link_recoveries = 0;
  }
  // A link that comes back and holds for kEarnOneMs earns back one quick recovery at once (the full refill above still
  // needs 5 minutes). Without it, a supply blip a minute after a hard boot found the budget spent and waited 5 minutes for
  // the slow retry (COM17, 2026-11-22: second device plugged into the hub). A flapping C6 still cannot loop quickly: each
  // earned recovery needs a connection that held first.
  if (*vars.connected && *vars.connected_since_ms && hooks.now_ms() - *vars.connected_since_ms >= kEarnOneMs) {
    if (!state.earned_one && *vars.link_recoveries) --*vars.link_recoveries;
    state.earned_one = true;
  }
  if (!*vars.connected) state.earned_one = false;
  if (state.slow_retries && *vars.connected && *vars.connected_since_ms &&
      hooks.now_ms() - *vars.connected_since_ms >= kBudgetRefillMs) {
    state.slow_retries = 0;
  }
  if (*vars.link_recovery_due_ms != 0 &&
      static_cast<int32_t>(hooks.now_ms() - *vars.link_recovery_due_ms) >= 0) {
    *vars.link_recovery_due_ms = 0;
    // A slow retry can come due after Wi-Fi has come back some other way (a manual reconnect, Wi-Fi switched off and on):
    // never tear down a working link for it.
    if (*vars.connected && !hooks.link_failed()) say(hooks.io, "RTL_WIFI_LINK_RETRY_SKIPPED reason=connected\n");
    else hooks.recover_link();
  }
  // Evidence for the Wi-Fi wedge: one line a minute while a remote receiver may depend on Wi-Fi, so a long soak shows what changed
  // before Wi-Fi died (link flags, retry budgets, how long it has been down, internal DMA RAM).
  const bool remote_may_need_wifi = hooks.remote_may_need_wifi();
  if (*vars.connected) state.down = false;
  else if (!state.down) {
    state.down = true;
    state.down_since_ms = hooks.now_ms();
  }
  if (remote_may_need_wifi && hooks.now_ms() - state.last_health_ms >= kHealthMs) {
    state.last_health_ms = hooks.now_ms();
    say(hooks.io,
        "RTL_WIFI_HEALTH connected=%d ready=%d link_failed=%d reconnect_attempts=%u recoveries=%u down_s=%lu "
        "dma_free=%lu stream=%s\n",
        *vars.connected ? 1 : 0, *vars.station_ready ? 1 : 0, hooks.link_failed() ? 1 : 0,
        static_cast<unsigned>(*vars.reconnect_attempts), static_cast<unsigned>(*vars.link_recoveries),
        static_cast<unsigned long>(state.down ? (hooks.now_ms() - state.down_since_ms) / 1000u : 0u),
        static_cast<unsigned long>(hooks.dma_free_bytes()), hooks.stream_state_name());
  }
  // Optional (Settings > RTL_TCP, off by default): reconnect and C6 link recovery are bounded, so a Wi-Fi outage longer than about
  // four minutes (an access point restarting) left the Tab5 offline until someone reset it. With this on, a remote receiver's Wi-Fi
  // gets a fresh budget every 5 minutes it stays down. It reuses the existing reconnect and recovery paths.
  if (remote_may_need_wifi && hooks.keepalive_enabled() && !*vars.connected && !*vars.connecting &&
      !*vars.scan_running && state.down && hooks.now_ms() - state.down_since_ms >= kKeepaliveDownMs &&
      hooks.now_ms() - state.last_rearm_ms >= kKeepaliveDownMs) {
    state.last_rearm_ms = hooks.now_ms();
    *vars.link_recoveries = 0;
    *vars.reconnect_attempts = 0;
    say(hooks.io, "RTL_WIFI_KEEPALIVE rearm down_s=%lu link_failed=%d\n",
        static_cast<unsigned long>((hooks.now_ms() - state.down_since_ms) / 1000u),
        hooks.link_failed() ? 1 : 0);
    if (hooks.link_failed()) state.link_failure_handled = false;  // run the C6 link recovery again
    else hooks.schedule_reconnect("keepalive");
  }
  hooks.resume_deferred_radio();
  if (hooks.link_failed() || *vars.link_recovery_due_ms != 0) return false;
  state.link_failure_handled = false;
  if (*vars.reconnect_due_ms != 0 &&
      static_cast<int32_t>(hooks.now_ms() - *vars.reconnect_due_ms) >= 0 &&
      !*vars.connecting && !*vars.connected && !*vars.scan_running) {
    *vars.reconnect_due_ms = 0;
    if (*vars.auto_reconnect_armed && hooks.profile_count()) {
      if (hooks.profile_ssid_empty()) hooks.select_first_profile();
      *vars.save_after_connect = false;
      hooks.request_connect();
      say(hooks.io, "RTL_WIFI_RECONNECT attempt=%u\n", static_cast<unsigned>(*vars.reconnect_attempts));
    }
  }
  return true;
}

bool user_connect_now(State& state, const Vars& vars, bool link_failed, uint32_t now_ms) {
  state.slow_retries = 0;
  *vars.link_recoveries = 0;
  *vars.reconnect_attempts = 0;
  if ((link_failed && state.link_failure_handled) || *vars.link_recovery_due_ms != 0) {
    *vars.link_recovery_due_ms = now_ms ? now_ms : 1;   // 0 means "none due"
    return true;
  }
  return false;   // link up, or a fresh failure the next service() handles (recovery in 2 s)
}

}  // namespace orcsdr::wifi_supervisor

#include "wifi_supervisor.hpp"

namespace orcsdr::wifi_supervisor {

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
      *vars.link_recovery_due_ms = hooks.now_ms() + kLostRecoveryDelayMs;
    } else {
      hooks.set_status("Wi-Fi link lost - restart Wi-Fi");
    }
    ++*vars.scan_revision;
    hooks.draw_state();
  }
  // A link that has stayed up for 5 minutes earns back its recovery budget,
  // so long-running devices keep recovering while a flapping C6 cannot loop.
  if (*vars.link_recoveries && *vars.connected && *vars.connected_since_ms &&
      hooks.now_ms() - *vars.connected_since_ms >= kBudgetRefillMs) {
    *vars.link_recoveries = 0;
  }
  if (*vars.link_recovery_due_ms != 0 &&
      static_cast<int32_t>(hooks.now_ms() - *vars.link_recovery_due_ms) >= 0) {
    *vars.link_recovery_due_ms = 0;
    hooks.recover_link();
  }
  // Evidence for the Wi-Fi wedge: one line a minute while a remote receiver may depend on Wi-Fi, so a long soak shows what changed
  // before Wi-Fi died (link flags, retry budgets, how long it has been down, internal DMA RAM).
  const bool remote_may_need_wifi = hooks.remote_may_need_wifi();
  if (*vars.connected) state.down_since_ms = 0;
  else if (state.down_since_ms == 0) state.down_since_ms = hooks.now_ms() | 1u;
  if (remote_may_need_wifi && hooks.now_ms() - state.last_health_ms >= kHealthMs) {
    state.last_health_ms = hooks.now_ms();
    say(hooks.io,
        "RTL_WIFI_HEALTH connected=%d ready=%d link_failed=%d reconnect_attempts=%u recoveries=%u down_s=%lu "
        "dma_free=%lu stream=%s\n",
        *vars.connected ? 1 : 0, *vars.station_ready ? 1 : 0, hooks.link_failed() ? 1 : 0,
        static_cast<unsigned>(*vars.reconnect_attempts), static_cast<unsigned>(*vars.link_recoveries),
        static_cast<unsigned long>(state.down_since_ms ? (hooks.now_ms() - state.down_since_ms) / 1000u : 0u),
        static_cast<unsigned long>(hooks.dma_free_bytes()), hooks.stream_state_name());
  }
  // Optional (Settings > RTL_TCP, off by default): reconnect and C6 link recovery are bounded, so a Wi-Fi outage longer than about
  // four minutes (an access point restarting) left the Tab5 offline until someone reset it. With this on, a remote receiver's Wi-Fi
  // gets a fresh budget every 5 minutes it stays down. It reuses the existing reconnect and recovery paths.
  if (remote_may_need_wifi && hooks.keepalive_enabled() && !*vars.connected && !*vars.connecting &&
      !*vars.scan_running && state.down_since_ms != 0 && hooks.now_ms() - state.down_since_ms >= kKeepaliveDownMs &&
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

}  // namespace orcsdr::wifi_supervisor

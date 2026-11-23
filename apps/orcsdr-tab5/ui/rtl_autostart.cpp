#include "rtl_autostart.hpp"

namespace orcsdr::rtl_autostart {

Start decide(State& state, const Hooks& hooks) {
  const bool boot_retry = state.started && hooks.capture_failed() &&
                          hooks.now_ms() - state.retry_ms >= kRetryMs;
  if ((!state.started || boot_retry) && hooks.boot_complete() && !hooks.io_paused() &&
      !hooks.usb_device_ready() && hooks.remote_source_ready() && !hooks.capture_running_or_queued() &&
      !hooks.capture_requested()) {
    state.started = true;
    state.retry_ms = hooks.now_ms();
    return boot_retry ? Start::retry : Start::first;
  }
  return Start::none;
}

}  // namespace orcsdr::rtl_autostart

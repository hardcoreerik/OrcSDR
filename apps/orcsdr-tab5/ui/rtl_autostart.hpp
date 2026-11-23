#pragma once
// When the receiver stream starts by itself at boot with a remote (rtl_tcp) source. Moved out of rtl_driver_app_task() in
// main.cpp. No device headers: the conditions are read through Hooks, in the order the old code read them, and the state is
// the two function-local statics it used.
#include <cstdint>

namespace orcsdr::rtl_autostart {

constexpr uint32_t kRetryMs = 30000;

struct State {
  bool started = false;      // a first attempt has been made this boot
  uint32_t retry_ms = 0;     // when the last attempt was made
};

// Every pointer must be set. Each is read only when the conditions before it held, as the old `&&` chain did.
struct Hooks {
  uint32_t (*now_ms)() = nullptr;
  bool (*capture_failed)() = nullptr;           // rtl_capture_state == failed
  bool (*boot_complete)() = nullptr;
  bool (*io_paused)() = nullptr;                // radio_io_paused()
  bool (*usb_device_ready)() = nullptr;
  bool (*remote_source_ready)() = nullptr;
  bool (*capture_running_or_queued)() = nullptr;
  bool (*capture_requested)() = nullptr;
};

enum class Start : uint8_t { none, first, retry };

// Decides whether to start the stream now. On first or retry the state is updated (started, retry time) and the caller
// queues the listen and prints its line.
Start decide(State& state, const Hooks& hooks);

}  // namespace orcsdr::rtl_autostart

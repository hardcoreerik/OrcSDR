#pragma once
// SSTV application glue: owns the dashboard lifecycle, settings, user actions and the hand-off
// of finished images to the background SD archive. main.cpp supplies only what it alone can do
// (RTL-SDR tuning, SD mount state, the preferences store, navigating home) through Hooks and
// forwards five calls: open(), close(), tick(), draw() and touch().
#include <cstdint>
#include "sstv_settings.hpp"

namespace orcsdr::sstv_app {

struct Hooks {
  bool (*receiver_ready)() = nullptr;                // an RTL-SDR is attached and usable
  bool (*streaming)() = nullptr;                     // IQ is flowing
  bool (*sd_ready)(bool mount_if_needed) = nullptr;  // SD card mounted (optionally try to mount)
  void (*tune)(uint32_t frequency_hz) = nullptr;     // point the RTL-SDR at an SSTV frequency
  void (*release_tuning)() = nullptr;                // undo tune() when the dashboard closes
  void (*go_home)() = nullptr;                       // navigate to the Home screen
  uint32_t (*utc_epoch)() = nullptr;                 // verified UTC seconds, 0 while unverified
  orcsdr::sstv_settings::Store store;                // persistent preferences
};

void install(const Hooks& hooks);   // call before open(); safe to repeat
void open();                        // dashboard opened: settings, receiver, archive, first draw, tune
void close();                       // dashboard left for any reason; no-op when it is not open
void tick();                        // main loop: decoder service and archive hand-off
void draw(bool static_panel);       // screen refresh requested by the screen controller
void touch(int32_t x, int32_t y);   // touch routing

}  // namespace orcsdr::sstv_app

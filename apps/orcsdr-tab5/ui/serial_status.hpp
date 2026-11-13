#pragma once
// The read-only status commands: RTL_STATUS, RTL_HOTPLUG_STATUS, RTL_ADSB STATUS, AUDIO_CODEC, RTL_DRIVER STATUS,
// RTL_AM_SCAN STATUS, UI_SELF_CHECK and HOME_PLOT STATUS. None of them needs authentication.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order and every reply are the ones the dispatcher had. Everything the commands read from the device
// comes through Hooks, so the family builds and is tested on the host; it keeps no state of its own. Each hook that
// returns a snapshot fills it in the order the original reply listed the values. RTL_ADSB STATUS, RTL_DRIVER STATUS
// and HOME_PLOT STATUS are printed by existing functions in main.cpp and the home screen; their hooks do that whole
// action.
#include <cstddef>
#include <cstdint>

namespace orcsdr::serial_status {

struct RtlStatus {
  bool connected = false;
  unsigned vid = 0;
  unsigned pid = 0;
  const char* speed = "";
  const char* serial = "";
};

struct HotplugStatus {
  int pending = 0;
  int ready = 0;
  const char* state = "";
  const char* band = "";
  unsigned frequency_hz = 0;
};

struct AudioState {   // the second half of the AUDIO_CODEC line
  int hp_detect = 0;
  int amp_pin = 0;
  int muted = 0;
  int running = 0;
  unsigned volume = 0;
};

struct AmScanStatus {
  int active = 0;
  unsigned step = 0;
  unsigned total = 0;
  unsigned found = 0;
  unsigned long frequency_hz = 0;
  int prompt = 0;
};

struct SelfCheck {
  int am = 0;
  int fm = 0;
  int home = 0;
  int shortwave = 0;
  int keypad = 0;
  int editor = 0;
};

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  RtlStatus (*rtl_status)() = nullptr;
  HotplugStatus (*hotplug_status)() = nullptr;
  void (*print_adsb_status)() = nullptr;            // prints the whole RTL_ADSB status itself
  uint8_t (*codec_register)(uint8_t reg) = nullptr; // one read of an ES8388 register
  AudioState (*audio_state)() = nullptr;
  void (*print_driver_status)() = nullptr;          // prints the whole RTL_DRIVER status itself
  AmScanStatus (*am_scan_status)() = nullptr;
  SelfCheck (*self_check)() = nullptr;
  void (*home_plot_status)(char* out, size_t size) = nullptr;   // fills a NUL-terminated line
};

// Returns true when `command` (not null) was one of these commands.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_status

#pragma once
// The FT8 control verbs: OPEN, BAND, MODE, CLEAR, RUN, TAB, HUNT, DIAL, CLOCKTEST, NTP, SNR, MEM, LOG and CONFIG.
// All of them need a session (FT8 SNR, MEM and LOG included, even when they only report). Parsing and reply text
// moved unchanged from process_ft8_command() in main.cpp; the decoder, the tuner, the settings and the log stay there
// behind Hooks.
#include <cstddef>
#include <cstdint>

#include "serial_ft8_io.hpp"

namespace orcsdr::serial_ft8_control {

enum class Mode : unsigned char { ft8, ft4 };

// The TAB names in the dashboard's tab order; main.cpp asserts the count matches orcsdr::ft8::Tab.
constexpr const char* kTabNames[] = {"LIVE", "DECODES", "MAP", "HUNTER", "HEARD", "SETUP"};
constexpr size_t kTabCount = sizeof(kTabNames) / sizeof(kTabNames[0]);

struct LogStatus {
  bool enabled = false;
  unsigned written = 0;
  unsigned staged = 0;
  unsigned dropped = 0;
  unsigned errors = 0;
  unsigned dma_largest = 0;
  unsigned need = 0;
};

// Every pointer must be set.
struct Hooks {
  orcsdr::serial_ft8::Io io;
  void (*open)() = nullptr;                                             // show the FT8 dashboard
  bool (*parse_band)(const char* text, size_t* index) = nullptr;        // index, label or dial Hz
  bool (*select_band)(size_t index) = nullptr;                          // from the band table
  // Label ("-" when the table has no such entry) and the dial frequency of `index` in the current mode.
  void (*band_info)(size_t index, const char** label, unsigned long* dial_hz) = nullptr;
  // Switches mode, drops any exact dial, retunes the selected band; reports the mode name and its dial frequency.
  void (*set_mode)(Mode mode, const char** name, unsigned long* dial_hz) = nullptr;
  void (*clear)() = nullptr;                                            // drain, empty the decode list
  bool (*run_start)() = nullptr;                                        // start the decoder and keep it running; false = start failed
  void (*run_stop)() = nullptr;
  void (*select_tab)(size_t index) = nullptr;                           // index into kTabNames
  bool (*hunt_start)(bool fast) = nullptr;
  void (*hunt_stop)() = nullptr;
  bool (*tune_dial)(uint32_t hz) = nullptr;
  void (*clock_test_release)() = nullptr;
  bool (*clock_test_step)(long delta_ms) = nullptr;                     // hold, forget the sync, step the clock
  bool (*ntp_start)() = nullptr;
  void (*set_snr_offset)(float db) = nullptr;                           // apply and save
  void (*reset_snr_offset)() = nullptr;                                 // 0 dB, saved
  double (*snr_offset)() = nullptr;
  void (*memory_report)(bool full) = nullptr;
  void (*log_load)() = nullptr;
  void (*log_set_enabled)(bool on) = nullptr;                           // apply and save
  bool (*log_flush)() = nullptr;
  void (*log_status)(LogStatus* out) = nullptr;
  bool (*set_config)(unsigned k, unsigned gate, unsigned fine_rows, unsigned long deadline_ms) = nullptr;
};

// `verb` is upper case, `rest` the remainder of the line without leading spaces. True when the verb was one of these
// (also when it was refused for lack of a session).
bool handle(const char* verb, const char* rest, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_ft8_control

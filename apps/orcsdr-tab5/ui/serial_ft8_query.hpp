#pragma once
// The FT8 read-only queries: FT8 STATUS, TIME, BANDS, DECODES [n], HUNTSTATUS and CLOCK. No sign-in needed.
// Parsing and reply text moved unchanged from process_ft8_command() in main.cpp; the decoder, the decode list, the
// band hunter and the clock stay there and are read through Hooks.
#include <cstddef>

#include "serial_ft8_io.hpp"

namespace orcsdr::serial_ft8_query {

struct Status {
  bool screen = false;
  const char* runtime = "";
  bool headless = false;
  const char* mode = "";
  const char* band = "-";
  unsigned long dial_hz = 0;
  bool clock = false;
  bool rx_running = false;
  bool tap = false;
  unsigned long rate_hz = 0;
  unsigned long long blocks = 0;
  unsigned long long ring = 0;
  unsigned long avg_us = 0;
  unsigned long max_us = 0;
  unsigned long slots = 0;
  unsigned long skipped = 0;
  unsigned long last_decodes = 0;
  unsigned long last_ms = 0;
  unsigned long spectral_ms = 0;
  unsigned long refine_ms = 0;
  unsigned long gate_ms = 0;
  unsigned coarse = 0;
  bool deadline_hit = false;
  unsigned long slot_rms = 0;
  unsigned long slot_peak = 0;
  unsigned long slot_clipped = 0;
  int dial_offset_hz = 0;
  double iq_dbfs = 0.0;
  double iq_clip_pct = 0.0;
  int gain_tenth_db = 0;
  unsigned store = 0;
  unsigned k = 0;
  unsigned gate = 0;
  unsigned fine_rows = 0;
  unsigned long deadline_ms = 0;
};

struct HuntSummary {
  const char* mode = "";
  const char* phase = "";
  int current = -1;             // band index, -1 for none
  int best = -1;
  unsigned slots_per_band = 0;
};

struct HuntBand {
  const char* label = "";
  bool visited = false;
  const char* evidence = "";
  unsigned slots = 0;
  unsigned decodes = 0;
  unsigned strong = 0;
  double peak_dbfs = 0.0;
};

struct Clock {
  unsigned level = 0;
  unsigned long age_s = 0;
  unsigned long ntp_utc = 0;
  unsigned long signal_utc = 0;
  bool wide = false;
  bool last_slot_wide = false;
  bool dt_valid = false;
  int dt_err_ms = 0;
  unsigned dt_stations = 0;
  bool hold = false;
};

// Every pointer must be set.
struct Hooks {
  orcsdr::serial_ft8::Io io;
  void (*status)(Status* out) = nullptr;                 // drains pending decodes first, then reads
  void (*time)(unsigned long long* utc_ms, bool* valid) = nullptr;
  size_t (*band_count)() = nullptr;
  // False when the table has no entry at `index` (the line is skipped).
  bool (*band)(size_t index, const char** label, unsigned long* dial_hz) = nullptr;
  void (*drain_decodes)() = nullptr;
  size_t (*decode_count)() = nullptr;
  bool (*print_decode)(size_t newest_index) = nullptr;   // one ORC_FT8_DECODE line; false when there is none
  void (*hunt_summary)(HuntSummary* out) = nullptr;
  bool (*hunt_band)(size_t index, HuntBand* out) = nullptr;   // false when the band is not part of the hunt
  void (*clock)(Clock* out) = nullptr;
};

// `verb` is upper case, `rest` the remainder of the line without leading spaces. True when the verb was one of these.
bool handle(const char* verb, const char* rest, const Hooks& hooks);

}  // namespace orcsdr::serial_ft8_query

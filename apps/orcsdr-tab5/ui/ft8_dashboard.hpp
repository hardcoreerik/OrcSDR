#pragma once

#include "ft8_model.hpp"
#include "ft8_hunter.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8 {

enum class Tab : uint8_t { live, decodes, map, hunter, heard, setup, count };
enum class DecoderState : uint8_t { unbound, armed, listening, decoding, ready, error };
enum class ActionKind : uint8_t {
  none,
  tune_band,
  clear_decodes,
  start_hunt_fast,
  start_hunt_decode,
  stop_hunt,
  lock_hunter_best,
  select_mode,         // value = a DigitalMode the user chose in SETUP
  tune_dial,           // value = a dial frequency in Hz typed or stepped in the Tune panel (expert tuning)
  tune_auto,           // back to the band table's dial for the current band
  set_expert,          // value = 1 or 0: expert tuning (Tune panel, OrcDial rotation steps the dial in Hz)
  gain_auto,           // receiver gain back to automatic
  gain_manual          // value = tuner gain in tenths of a dB (one of the receiver's steps)
};

struct Action {
  ActionKind kind = ActionKind::none;
  uint32_t value = 0;
};

struct Snapshot {
  uint64_t utc_ms = 0;
  bool clock_valid = false;
  bool receiver_running = false;
  bool gain_auto = true;
  int16_t gain_tenth_db = 0;
  // Receiver gain control (Live GAIN chip). The step table comes from the driver; the level and clipping are measured on the raw IQ.
  bool gain_available = false;
  uint8_t gain_step_count = 0;
  int16_t gain_steps_tenth_db[32]{};
  float iq_dbfs = -90.0f;        // smoothed input level, 0 = full scale
  float iq_clip_pct = 0.0f;      // smoothed share of IQ samples at full scale
  int16_t battery_percent = -1;
  uint8_t candidate_count = 0;
  uint8_t last_slot_decodes = 0;
  size_t selected_band = 5;  // 20 m
  DigitalMode mode = DigitalMode::ft8;
  uint32_t decoder_capabilities = 0;   // DecoderCapability bits; 0 = no decoder bound
  DecoderState decoder_state = DecoderState::unbound;
  HunterSnapshot hunter{};
  // The receiver's own position (from the setup wizard / settings) for DIST and BRG; unknown = those cells show a dash.
  // Live waterfall rows (0-255, newest = (wf_sequence - 1) % wf_rows); null until a decoder provides them.
  const uint8_t* waterfall = nullptr;
  uint16_t wf_rows = 0;
  uint16_t wf_bins = 0;
  uint32_t wf_sequence = 0;
  // Expert tuning. dial_hz is what the receiver is actually tuned to (band table or custom); dial_custom marks a typed/stepped dial.
  bool expert_tuning = false;
  bool dial_custom = false;
  uint32_t dial_hz = 0;
  bool station_known = false;
  float station_latitude = 0.0f;
  float station_longitude = 0.0f;
  Decode decodes[kDecodeCapacity]{};
  size_t decode_count = 0;
};

// What the application's persistent stations-heard table knows about one callsign (first/last heard etc.). The dashboard never owns that table.
struct HeardInfo {
  char callsign[12]{};
  char grid[8]{};
  uint32_t first_utc = 0;
  uint32_t last_utc = 0;
  uint32_t count = 0;
  uint16_t band_mask = 0;   // bit n = band-table index n
  uint8_t mode_mask = 0;    // bit n = DigitalMode n
  int8_t best_snr = 0;
  int8_t last_snr = 0;
  bool snr_known = false;
  bool worked = false;
};
using HeardLookup = bool (*)(const char* callsign, HeardInfo* out);
void set_heard_lookup(HeardLookup lookup);   // optional: without it a tap on a station does nothing

void enter(const Snapshot& snapshot);
void update(const Snapshot& snapshot);
// Scrolls waiting waterfall rows into the Live tab at a steady pace between full updates (no-op on other tabs).
void pump_waterfall(uint32_t sequence);
void draw();
Action handle_touch(int32_t x, int32_t y);
void leave();
bool active();
Tab tab();
// The shared header controls (Home icon, volume, mute, visualizer, settings, battery) belong to the application; it
// installs the function that draws them, and the dashboard calls it whenever the header is repainted.
void set_header_hook(void (*draw_controls)());
void select_tab(Tab tab);   // from OrcDial; redraws when the tab changes
bool open_decode_item(size_t one_based); // selected newest-first decode's station history
const Snapshot& snapshot();
// The step the Tune panel is set to, in Hz (OrcDial rotation uses the same step while expert tuning is on).
uint32_t tune_step_hz();
bool dashboard_self_check();

}  // namespace orcsdr::ft8

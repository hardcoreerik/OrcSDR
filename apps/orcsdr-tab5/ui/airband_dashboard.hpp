#pragma once

#include "airband_catalog.hpp"
#include "airband_scanner.hpp"
#include "receiver_tuning_controls.hpp"

#include <cstddef>
#include <cstdint>
#include <new>

namespace orcsdr::airband {

enum class Tab : uint8_t { listen, scan, airports, activity, setup, scope };

struct Snapshot {
  uint32_t now_ms = 0;
  uint32_t frequency_hz = kGuardFrequencyHz;
  float channel_db = -120.0f;  // in-channel carrier level, dBFS
  float snr_db = 0.0f;         // carrier above the tracked noise floor
  float floor_db = -120.0f;
  bool squelch_open = false;
  bool running = false;
  bool sound_enabled = true;
  int32_t battery_percent = -1;
  char current_label[48]{};

  ScanState scan_state = ScanState::off;
  Settings scan{};
  uint32_t hang_remaining_ms = 0;
  uint32_t stops = 0;
  uint32_t channels_checked = 0;

  bool catalog_loaded = false;
  LoadResult load_result = LoadResult::not_loaded;
  bool location_configured = false;
  receiver_controls::State controls{};
  int16_t gain_steps_tenth_db[32]{};
  uint8_t gain_step_count = 0;

  uint8_t catalog_count = 0;
  CatalogEntry catalog[Catalog::kCapacity]{};

  uint8_t bank_count = 0;
  BankEntry bank[kBankCapacity]{};

  uint8_t activity_count = 0;
  Activity activity[kActivityCapacity]{};
};

enum class ActionKind : uint8_t {
  none,
  tune_down,
  tune_up,
  tune_guard,
  scan_toggle,
  hold_toggle,
  skip,
  tune_catalog,
  source_cycle,
  spacing_cycle,
  radius_cycle,
  gain_down,
  gain_up,
  tuner_agc_toggle,
  rtl_agc_toggle,
  squelch_down,
  squelch_up,
  settle_cycle,
  hang_down,
  hang_up,
  priority_toggle,
  reload_catalog,
  clear_activity,
  open_settings,
  open_location_settings,
  exit_home,
  tune_to,   // value = frequency in Hz (tap on the scope)
  span_down,
  span_up,
};

struct Action {
  ActionKind kind = ActionKind::none;
  int32_t value = 0;
};

// Re-initialises a static Snapshot in place (it is several KB, so avoid `= {}` temporaries).
inline void reset_snapshot(Snapshot& snapshot) {
  snapshot.~Snapshot();
  new (&snapshot) Snapshot();
}

void dashboard_enter(const Snapshot& snapshot);
void dashboard_leave();
void dashboard_draw();
void dashboard_update(const Snapshot& snapshot);
Action dashboard_handle_touch(int32_t x, int32_t y);
void dashboard_select_tab(Tab tab);
bool dashboard_active();
// The SCOPE tab draws a live spectrum and waterfall from the shared spectrum pipeline.
bool dashboard_spectrum_active();
void dashboard_set_scope_span_hook(void (*hook)(uint32_t hz));
uint32_t dashboard_scope_fps();       // spectrum frames drawn in the last second
uint32_t dashboard_scope_draw_ms();   // time the last frame took to draw
void dashboard_draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                             uint32_t span_hz);
Tab dashboard_tab();
bool dashboard_self_check();

}  // namespace orcsdr::airband

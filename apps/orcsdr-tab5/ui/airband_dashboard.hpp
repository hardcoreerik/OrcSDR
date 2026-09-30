#pragma once

#include "airband_catalog.hpp"
#include "airband_scanner.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::airband {

enum class Tab : uint8_t { listen, scan, airports, activity, setup };

struct Snapshot {
  uint32_t now_ms = 0;
  uint32_t frequency_hz = kGuardFrequencyHz;
  float signal_dbfs = -120.0f;
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
  bool location_configured = false;
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
};

struct Action {
  ActionKind kind = ActionKind::none;
  int32_t value = 0;
};

void dashboard_enter(const Snapshot& snapshot);
void dashboard_leave();
void dashboard_draw();
void dashboard_update(const Snapshot& snapshot);
Action dashboard_handle_touch(int32_t x, int32_t y);
bool dashboard_active();
Tab dashboard_tab();
bool dashboard_self_check();

}  // namespace orcsdr::airband

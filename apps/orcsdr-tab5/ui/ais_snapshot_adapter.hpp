#pragma once
// Snapshot translation from validated AIS station state. Host-testable.
// DSP task owns StationStore; caller must synchronize/copy before invoking
// this on another task. Never call UI directly inside RF packet callback.
#include "ais_dashboard.hpp"
#include "ais_station_store.hpp"
#include <cstdint>

namespace orcsdr::ais_ui {
// Updates only snapshot.targets and target_count; preserves other dashboard
// fields and revision. No allocation or invented static/voyage fields.
// Selects freshest kTargets observations deterministically when store is fuller.
void populate_targets(const ais::StationStore& stations,
                      uint64_t now_ms,uint64_t stale_after_ms,Snapshot* snapshot);
} // namespace orcsdr::ais_ui

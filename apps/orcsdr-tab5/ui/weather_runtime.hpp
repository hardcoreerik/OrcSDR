#pragma once

#include "weather_dashboard.hpp"
#include "weather_report_store.hpp"

namespace orcsdr { class NvsStore; }
namespace orcsdr::storage { class FileSystem; }

namespace orcsdr::weather {

void runtime_initialize(NvsStore* preferences, storage::FileSystem* filesystem);
OnlinePolicy runtime_online_policy();
bool runtime_cycle_online_policy();
void runtime_set_scan_result(const NoaaScanResult& result);
const NoaaScanResult& runtime_scan_result();
bool runtime_save_snapshot(const Snapshot& snapshot, ReportSaveResult* result);
void runtime_fill_report_state(Snapshot* snapshot);
bool runtime_self_check();

}  // namespace orcsdr::weather

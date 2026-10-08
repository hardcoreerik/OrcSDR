#include "weather_runtime.hpp"

#include "nvs_store.hpp"
#include "orcsdr_storage.hpp"
#include "time_service.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::weather {
namespace {
NvsStore* g_preferences = nullptr;
storage::FileSystem* g_fs = nullptr;
OnlinePolicy g_policy = OnlinePolicy::disabled;
NoaaScanResult g_scan{};
constexpr char kPolicyKey[] = "wx_net";
}

void runtime_initialize(NvsStore* preferences, storage::FileSystem* filesystem) {
  g_preferences = preferences;
  g_fs = filesystem;
  g_policy = OnlinePolicy::disabled;
  if (g_preferences) {
    const uint8_t raw = g_preferences->getUChar(kPolicyKey, 0);
    if (valid_online_policy(raw)) g_policy = static_cast<OnlinePolicy>(raw);
  }
}

OnlinePolicy runtime_online_policy() { return g_policy; }

bool runtime_cycle_online_policy() {
  const uint8_t next = (static_cast<uint8_t>(g_policy) + 1u) % 3u;
  g_policy = static_cast<OnlinePolicy>(next);
  return !g_preferences || g_preferences->putUChar(kPolicyKey, next);
}

void runtime_set_scan_result(const NoaaScanResult& result) { g_scan = result; }
const NoaaScanResult& runtime_scan_result() { return g_scan; }

bool runtime_save_snapshot(const Snapshot& snapshot, ReportSaveResult* result) {
  if (!g_fs) {
    if (result) { *result={}; std::snprintf(result->error,sizeof(result->error),"SD unavailable"); }
    return false;
  }
  const auto now = time_service::now();
  ReportSnapshot report{};
  report.created_utc = now.utc;
  report.created_uptime_ms = now.uptime_ms;
  report.wallclock_valid = now.wallclock_valid;
  report.online_policy = g_policy;
  report.noaa_frequency_hz = snapshot.current_noaa_hz;
  report.noaa_dbfs = snapshot.relative_dbfs;
  report.noaa_valid = snapshot.rf_running || snapshot.noaa_scan.complete;
  if (snapshot.location_configured)
    std::snprintf(report.location_label,sizeof(report.location_label),"%s",snapshot.location_label);
  else
    std::snprintf(report.location_label,sizeof(report.location_label),"location not configured");
  return save_report_bundle(*g_fs, report, result);
}

void runtime_fill_report_state(Snapshot* snapshot) {
  if (!snapshot) return;
  snapshot->online_policy = g_policy;
  snapshot->noaa_scan = g_scan;
  if (!g_fs) return;
  snapshot->report_count = report_history_count(*g_fs);
  (void)latest_report_id(*g_fs, snapshot->last_report_id, sizeof(snapshot->last_report_id));
}

bool runtime_self_check() {
  return default_online_policy() == OnlinePolicy::disabled &&
         valid_online_policy(0) && valid_online_policy(1) && valid_online_policy(2) &&
         !valid_online_policy(3);
}

}  // namespace orcsdr::weather

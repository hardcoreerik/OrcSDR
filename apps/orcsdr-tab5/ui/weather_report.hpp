#pragma once

#include "weather_model.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::weather {

struct ReportSnapshot {
  uint32_t created_utc = 0;
  uint32_t created_uptime_ms = 0;
  bool wallclock_valid = false;
  OnlinePolicy online_policy = OnlinePolicy::disabled;
  uint32_t noaa_frequency_hz = 0;
  float noaa_dbfs = 0.0f;
  bool noaa_valid = false;
  uint32_t noaa_age_seconds = 0;
  char location_label[64]{};
};

bool encode_report_json(const ReportSnapshot& report, char* output, size_t capacity);
bool encode_report_csv(const ReportSnapshot& report, char* output, size_t capacity);
bool encode_report_html(const ReportSnapshot& report, char* output, size_t capacity);
const char* online_policy_label(OnlinePolicy policy);

}  // namespace orcsdr::weather

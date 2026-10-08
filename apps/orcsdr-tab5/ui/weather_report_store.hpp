#pragma once

#include "weather_report.hpp"

#include <cstddef>

namespace orcsdr::storage { class FileSystem; }

namespace orcsdr::weather {

struct ReportSaveResult {
  bool ok = false;
  char id[40]{};
  char error[48]{};
};

bool save_report_bundle(storage::FileSystem& fs, const ReportSnapshot& report,
                        ReportSaveResult* result);
size_t report_history_count(storage::FileSystem& fs);
bool latest_report_id(storage::FileSystem& fs, char* output, size_t capacity);

}  // namespace orcsdr::weather

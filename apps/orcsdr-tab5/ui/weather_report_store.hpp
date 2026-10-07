#pragma once

#include "orcsdr_storage.hpp"
#include "weather_model.hpp"
#include "weather_report_format.hpp"
#include "weather_service.hpp"

#include <cstddef>

namespace orcsdr::weather::report_store {

constexpr char kRoot[] = "/orcsdr/weather";
constexpr char kReportsRoot[] = "/orcsdr/weather/reports";
constexpr char kHistoryPath[] = "/orcsdr/weather/history.jsonl";

struct SaveRequest {
  report::Session session{};
  const Observation* observations = nullptr;
  size_t observation_count = 0;
  ServiceState rf{};
  char location[40]{};
};

struct SaveResult {
  bool ok = false;
  bool history_recorded = false;
  char report_path[128]{};
};

bool save_snapshot(storage::FileSystem& fs, const SaveRequest& request,
                   SaveResult* result, char* error, size_t error_capacity);
size_t count_history(storage::FileSystem& fs, size_t limit = 255);

}  // namespace orcsdr::weather::report_store

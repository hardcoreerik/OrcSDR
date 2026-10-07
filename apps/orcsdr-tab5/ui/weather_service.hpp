#pragma once

#include "weather_noaa.hpp"
#include <cstdint>

namespace orcsdr::weather {

enum class RfState : uint8_t { idle, listening, scanning };

struct ServiceState {
  RfState rf_state = RfState::idle;
  uint8_t selected_channel = 0;
  uint32_t active_frequency_hz = 0;
  bool scan_complete = false;
  uint8_t strongest_channel = 0;
  uint32_t strongest_frequency_hz = 0;
  uint32_t last_scan_uptime_ms = 0;
  uint8_t scan_samples = 0;
};

class Service {
 public:
  void enter();
  void leave();
  bool select_channel(uint8_t index);
  uint32_t selected_frequency_hz() const;
  void begin_listen();
  void stop_rf();
  void begin_scan(uint32_t now_ms);
  void cancel_scan();
  uint32_t scan_frequency_hz() const;
  void record_scan_sample(float level_dbfs, uint32_t now_ms);
  bool advance_scan();
  const ServiceState& state() const { return state_; }

 private:
  ServiceState state_{};
  noaa::ScanPlan scan_{};
};

}  // namespace orcsdr::weather

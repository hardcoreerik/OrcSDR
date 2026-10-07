#include "weather_service.hpp"

namespace orcsdr::weather {

void Service::enter() {}

void Service::leave() {
  if (state_.rf_state == RfState::scanning) scan_.cancel();
  state_.rf_state = RfState::idle;
  state_.active_frequency_hz = 0;
}

bool Service::select_channel(uint8_t index) {
  if (index >= noaa::kChannelCount) return false;
  state_.selected_channel = index;
  return true;
}

uint32_t Service::selected_frequency_hz() const {
  return noaa::channel_hz(state_.selected_channel);
}

void Service::begin_listen() {
  state_.rf_state = RfState::listening;
  state_.active_frequency_hz = selected_frequency_hz();
  state_.scan_complete = false;
}

void Service::stop_rf() {
  if (state_.rf_state == RfState::scanning) scan_.cancel();
  state_.rf_state = RfState::idle;
  state_.active_frequency_hz = 0;
}

void Service::begin_scan(uint32_t) {
  scan_.start();
  state_.rf_state = RfState::scanning;
  state_.active_frequency_hz = scan_.current_frequency_hz();
  state_.scan_complete = false;
  state_.strongest_frequency_hz = 0;
  state_.strongest_channel = 0;
  state_.scan_samples = 0;
}

void Service::cancel_scan() {
  scan_.cancel();
  state_.rf_state = RfState::idle;
  state_.active_frequency_hz = 0;
}

uint32_t Service::scan_frequency_hz() const {
  return state_.rf_state == RfState::scanning ? scan_.current_frequency_hz() : 0;
}

void Service::record_scan_sample(float level_dbfs, uint32_t now_ms) {
  if (state_.rf_state != RfState::scanning) return;
  scan_.offer(level_dbfs);
  state_.scan_samples = static_cast<uint8_t>(scan_.sample_count());
  state_.last_scan_uptime_ms = now_ms;
}

bool Service::advance_scan() {
  if (state_.rf_state != RfState::scanning) return false;
  scan_.advance();
  if (scan_.complete()) {
    state_.scan_complete = true;
    const int strongest = scan_.strongest_index();
    state_.strongest_channel = strongest >= 0 ? static_cast<uint8_t>(strongest) : 0;
    state_.strongest_frequency_hz = scan_.strongest_frequency_hz();
    state_.selected_channel = state_.strongest_channel;
    state_.active_frequency_hz = state_.strongest_frequency_hz;
    state_.rf_state = state_.active_frequency_hz ? RfState::listening : RfState::idle;
    return false;
  }
  state_.active_frequency_hz = scan_.current_frequency_hz();
  return true;
}

}  // namespace orcsdr::weather

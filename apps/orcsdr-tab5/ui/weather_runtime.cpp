#include "weather_runtime.hpp"

namespace orcsdr::weather {

ReceiverCommand Runtime::enter() {
  service_.enter();
  return {};
}

ReceiverCommand Runtime::leave() {
  const bool active = service_.state().rf_state != RfState::idle;
  service_.leave();
  return active ? ReceiverCommand{ReceiverCommandKind::stop_owned, 0} : ReceiverCommand{};
}

bool Runtime::select_channel(uint8_t index) { return service_.select_channel(index); }

bool Runtime::previous_channel() {
  const uint32_t current = service_.selected_frequency_hz();
  const int index = noaa::channel_index(noaa::previous_channel_hz(current));
  return index >= 0 && service_.select_channel(static_cast<uint8_t>(index));
}

bool Runtime::next_channel() {
  const uint32_t current = service_.selected_frequency_hz();
  const int index = noaa::channel_index(noaa::next_channel_hz(current));
  return index >= 0 && service_.select_channel(static_cast<uint8_t>(index));
}

ReceiverCommand Runtime::listen() {
  service_.begin_listen();
  return {ReceiverCommandKind::start_foreground, service_.selected_frequency_hz()};
}

ReceiverCommand Runtime::scan(uint32_t now_ms) {
  service_.begin_scan(now_ms);
  return {ReceiverCommandKind::start_foreground, service_.scan_frequency_hz()};
}

ReceiverCommand Runtime::stop() {
  const bool active = service_.state().rf_state != RfState::idle;
  service_.stop_rf();
  return active ? ReceiverCommand{ReceiverCommandKind::stop_owned, 0} : ReceiverCommand{};
}

void Runtime::record_scan_sample(float level_dbfs, uint32_t now_ms) {
  if (service_.state().rf_state != RfState::scanning) return;
  service_.record_scan_sample(level_dbfs, now_ms);
  (void)service_.advance_scan();
}

ReceiverCommand Runtime::finish_scan(bool completed) {
  if (!completed) {
    service_.cancel_scan();
    return {};
  }
  const uint32_t frequency_hz = service_.state().strongest_frequency_hz;
  return frequency_hz ? ReceiverCommand{ReceiverCommandKind::retune_owned, frequency_hz}
                      : ReceiverCommand{};
}

}  // namespace orcsdr::weather

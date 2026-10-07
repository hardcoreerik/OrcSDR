#include "weather_runtime.hpp"

namespace orcsdr::weather {

ReceiverCommand Runtime::enter() {
  service_.enter();
  return {};
}

ReceiverCommand Runtime::leave() {
  const bool active = service_.state().rf_state != RfState::idle;
  service_.leave();
  scan_due_ms_ = 0;
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
  scan_due_ms_ = 0;
  return {ReceiverCommandKind::start_foreground, service_.selected_frequency_hz()};
}

ReceiverCommand Runtime::scan(uint32_t now_ms) {
  service_.begin_scan(now_ms);
  scan_due_ms_ = now_ms + kScanDwellMs;
  return {ReceiverCommandKind::start_foreground, service_.scan_frequency_hz()};
}

ReceiverCommand Runtime::stop() {
  const bool active = service_.state().rf_state != RfState::idle;
  service_.stop_rf();
  scan_due_ms_ = 0;
  return active ? ReceiverCommand{ReceiverCommandKind::stop_owned, 0} : ReceiverCommand{};
}

ReceiverCommand Runtime::service(uint32_t now_ms, float signal_dbfs) {
  if (service_.state().rf_state != RfState::scanning || scan_due_ms_ == 0 ||
      static_cast<int32_t>(now_ms - scan_due_ms_) < 0)
    return {};

  service_.record_scan_sample(signal_dbfs, now_ms);
  const bool more = service_.advance_scan();
  scan_due_ms_ = more ? now_ms + kScanDwellMs : 0;
  const uint32_t next = more ? service_.scan_frequency_hz()
                             : service_.state().strongest_frequency_hz;
  return next ? ReceiverCommand{ReceiverCommandKind::retune_owned, next}
              : ReceiverCommand{};
}

}  // namespace orcsdr::weather

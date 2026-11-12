#include "iot_sensor_discovery.hpp"

#include <cstring>
#include <limits>

namespace orcsdr::iot {
namespace {
bool safe_name(const char* src, size_t capacity) {
  if (src == nullptr || src[0] == 0) return false;
  for (size_t i = 0; i < capacity; ++i) {
    const unsigned char c = static_cast<unsigned char>(src[i]);
    if (c == 0) return true;
    if (c < 32 || c > 126) return false;
  }
  return false;  // reject truncation rather than conflating IDs
}

void reset_device(Device& d) {
  std::memset(d.protocol, 0, sizeof(d.protocol));
  std::memset(d.sensor_id, 0, sizeof(d.sensor_id));
  d.sensor_class = SensorClass::unknown;
  d.confidence = Confidence::tentative;
  d.latest_reading_integrity_verified = false;
  d.center_hz = 0;
  d.first_seen_ms = 0;
  d.last_seen_ms = 0;
  d.packets = 0;
  d.rssi_valid = false;
  d.rssi_dbm = 0;
  d.reading_count = 0;
  for (auto& reading : d.readings) reading = Reading{};
}

void saturate_inc(uint32_t& number) {
  if (number < std::numeric_limits<uint32_t>::max()) ++number;
}

bool valid_metrics(const DecodedEvent& e) {
  if (e.reading_count > kReadingCapacity) return false;
  for (uint8_t i = 0; i < e.reading_count; ++i) {
    if (static_cast<uint8_t>(e.readings[i].metric) >
        static_cast<uint8_t>(Metric::switch_state)) return false;
    if (e.readings[i].metric == Metric::switch_state &&
        e.readings[i].milli_units != 0 && e.readings[i].milli_units != 1000)
      return false;
  }
  return true;
}
}  // namespace

void Inventory::clear() {
  // Counters and entries intentionally reset together. No dangling device pointers.
  for (auto& device : devices_) reset_device(device);
  count_ = 0;
  unclassified_ = 0;
  unattributed_ = 0;
  invalid_ = 0;
  evictions_ = 0;
}

IngestResult Inventory::ingest(const DecodedEvent& event) {
  if (event.center_hz == 0 || !valid_metrics(event) ||
      (event.integrity_checked && !event.integrity_ok)) {
    saturate_inc(invalid_);
    return IngestResult::rejected;
  }
  if (!safe_name(event.protocol, kProtocolChars)) {
    saturate_inc(unclassified_);
    return IngestResult::unclassified_signal;
  }
  if (!safe_name(event.sensor_id, kIdentityChars)) {
    // A named protocol without a stable ID is an event, not a discoverable device.
    saturate_inc(unattributed_);
    return IngestResult::unattributed_decode;
  }

  size_t slot = count_;
  for (size_t i = 0; i < count_; ++i) {
    const Device& d = devices_[i];
    if (d.center_hz == event.center_hz &&
        std::strcmp(d.protocol, event.protocol) == 0 &&
        std::strcmp(d.sensor_id, event.sensor_id) == 0) {
      slot = i;
      break;
    }
  }

  const bool existing = slot < count_;
  if (!existing) {
    if (count_ < kDeviceCapacity) {
      slot = count_++;
    } else {
      // Replace least-recently-seen entry; wrap-safe for reasonable uptime windows.
      slot = 0;
      uint32_t oldest_age = 0;
      for (size_t i = 0; i < count_; ++i) {
        const uint32_t age = event.received_ms - devices_[i].last_seen_ms;
        if (age >= oldest_age) { oldest_age = age; slot = i; }
      }
      saturate_inc(evictions_);
    }
    reset_device(devices_[slot]);
    Device& d = devices_[slot];
    std::strcpy(d.protocol, event.protocol);  // checked bounded ASCII above
    std::strcpy(d.sensor_id, event.sensor_id);
    d.center_hz = event.center_hz;
    d.first_seen_ms = event.received_ms;
  }
  Device& d = devices_[slot];
  d.last_seen_ms = event.received_ms;
  if (event.sensor_class != SensorClass::unknown)
    d.sensor_class = event.sensor_class;
  if (event.integrity_checked && event.integrity_ok)
    d.confidence = Confidence::integrity_verified; // never downgrade
  saturate_inc(d.packets);
  d.rssi_valid = event.rssi_valid;
  if (event.rssi_valid) d.rssi_dbm = event.rssi_dbm;
  d.latest_reading_integrity_verified = event.integrity_checked && event.integrity_ok;
  d.reading_count = event.reading_count;
  for (uint8_t i = 0; i < event.reading_count; ++i)
    d.readings[i] = event.readings[i];
  return existing ? IngestResult::updated : IngestResult::inserted;
}

bool Inventory::stale(size_t index, uint32_t now_ms, uint32_t max_age_ms) const {
  if (index >= count_) return true;
  return static_cast<uint32_t>(now_ms - devices_[index].last_seen_ms) > max_age_ms;
}

}  // namespace orcsdr::iot

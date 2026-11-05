#pragma once
// Portable, bounded IoT sensor discovery model. No SDR, RTOS, display, or heap dependency.
// Decoder outputs are untrusted: only validated named protocols get a stable device record.
#include <cstddef>
#include <cstdint>

namespace orcsdr::iot {

constexpr size_t kDeviceCapacity = 32;
constexpr size_t kReadingCapacity = 4;
constexpr size_t kProtocolChars = 28;
constexpr size_t kIdentityChars = 24;

enum class SensorClass : uint8_t {
  unknown, environmental, machinery, utility_meter, alarm, vehicle, other
};
enum class Metric : uint8_t {
  temperature_c, humidity_percent, pressure_kpa, vibration_mm_s,
  voltage_v, battery_percent, operating_hours, switch_state
};
enum class Confidence : uint8_t { tentative, integrity_verified };
enum class IngestResult : uint8_t {
  rejected, unclassified_signal, unattributed_decode, inserted, updated
};

// The decoder must supply a trustworthy protocol name, never guessed from frequency alone.
// One integer means 1/1000 of the unit named by Metric (e.g. 24,500 = 24.5 C).
// Negative values are supported. switch_state is 0 or 1, also milli-scaled.
struct Reading {
  Metric metric = Metric::temperature_c;
  int32_t milli_units = 0;
};

struct DecodedEvent {
  uint32_t received_ms = 0;             // monotonic device uptime
  uint32_t center_hz = 0;               // receiver's actual tuned center
  const char* protocol = nullptr;
  const char* sensor_id = nullptr;      // protocol-derived ID, not guessed MAC/serial
  SensorClass sensor_class = SensorClass::unknown;
  bool integrity_checked = false;      // decoder verified actual checksum/CRC/etc.
  bool integrity_ok = false;           // false with checked=true means reject
  bool rssi_valid = false;
  int16_t rssi_dbm = 0;
  uint8_t reading_count = 0;
  Reading readings[kReadingCapacity]{};
};

struct Device {
  char protocol[kProtocolChars]{};
  char sensor_id[kIdentityChars]{};
  SensorClass sensor_class = SensorClass::unknown;
  Confidence confidence = Confidence::tentative;  // historical strongest device identity evidence
  bool latest_reading_integrity_verified = false;  // never inherit validation from an older packet
  uint32_t center_hz = 0;
  uint32_t first_seen_ms = 0;
  uint32_t last_seen_ms = 0;
  uint32_t packets = 0;
  bool rssi_valid = false;
  int16_t rssi_dbm = 0;
  uint8_t reading_count = 0;
  Reading readings[kReadingCapacity]{};
};

class Inventory {
 public:
  // Called from the owning task, not concurrently from the high-rate IQ callback.
  IngestResult ingest(const DecodedEvent& event);
  void clear();
  size_t count() const { return count_; }
  const Device* at(size_t index) const { return index < count_ ? &devices_[index] : nullptr; }
  uint32_t signals_unclassified() const { return unclassified_; }
  uint32_t decodes_unattributed() const { return unattributed_; }
  uint32_t invalid_events() const { return invalid_; }
  uint32_t evictions() const { return evictions_; }
  bool stale(size_t index, uint32_t now_ms, uint32_t max_age_ms = 900000u) const;
 private:
  Device devices_[kDeviceCapacity]{};
  size_t count_ = 0;
  uint32_t unclassified_ = 0;
  uint32_t unattributed_ = 0;
  uint32_t invalid_ = 0;
  uint32_t evictions_ = 0;
};

}  // namespace orcsdr::iot

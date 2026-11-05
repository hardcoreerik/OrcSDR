// Host test: g++ -std=c++17 -Wall -Wextra -Werror -pedantic -I apps/orcsdr-tab5/ui
// apps/orcsdr-tab5/ui/iot_sensor_discovery.cpp apps/orcsdr-tab5/tests/iot_sensor_discovery_test.cpp
// -o /tmp/orcsdr-iot-test && /tmp/orcsdr-iot-test
#include "iot_sensor_discovery.hpp"
#include "iot_sensor_scan_profiles.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace orcsdr::iot;

int main() {
  Inventory store;
  DecodedEvent event{};
  event.center_hz = 433920000u;
  event.received_ms = 100;
  event.protocol = "Example-OOK";
  event.sensor_id = "0042";
  event.sensor_class = SensorClass::machinery;
  event.reading_count = 2;
  event.readings[0] = {Metric::temperature_c, 24500};
  event.readings[1] = {Metric::vibration_mm_s, 3600};

  assert(store.ingest(event) == IngestResult::inserted);
  assert(store.count() == 1);
  assert(store.at(0)->confidence == Confidence::tentative);
  assert(!store.at(0)->latest_reading_integrity_verified);
  assert(store.at(0)->readings[1].milli_units == 3600);
  assert(store.at(0)->packets == 1);
  assert(!store.stale(0, 100));
  assert(store.stale(0, 1000000));

  event.received_ms = 200;
  event.integrity_checked = true;
  event.integrity_ok = true;
  assert(store.ingest(event) == IngestResult::updated);
  assert(store.at(0)->confidence == Confidence::integrity_verified);
  assert(store.at(0)->latest_reading_integrity_verified);
  assert(store.at(0)->packets == 2);

  event.received_ms = 300;
  event.integrity_checked = false;
  event.integrity_ok = false;
  event.sensor_class = SensorClass::unknown;
  assert(store.ingest(event) == IngestResult::updated);
  assert(store.at(0)->confidence == Confidence::integrity_verified);
  assert(!store.at(0)->latest_reading_integrity_verified);
  assert(store.at(0)->sensor_class == SensorClass::machinery);

  event.integrity_checked = true;
  event.integrity_ok = false;
  assert(store.ingest(event) == IngestResult::rejected);
  assert(store.at(0)->packets == 3);

  event.integrity_checked = false;
  event.sensor_id = nullptr;
  assert(store.ingest(event) == IngestResult::unattributed_decode);
  event.sensor_id = "0042";
  event.protocol = nullptr;
  assert(store.ingest(event) == IngestResult::unclassified_signal);
  event.protocol = "Example-OOK";
  event.reading_count = static_cast<uint8_t>(kReadingCapacity + 1);
  assert(store.ingest(event) == IngestResult::rejected);
  event.reading_count = 0;
  event.center_hz = 915000000u;
  assert(store.ingest(event) == IngestResult::inserted);
  assert(store.count() == 2);  // same ID but different channel is not merged

  char id[24]{};
  event.center_hz = 315000000u;
  event.integrity_checked = true;
  event.integrity_ok = true;
  event.received_ms = 1000;
  for (int i = 0; i < 40; ++i) {
    std::snprintf(id, sizeof(id), "device-%02d", i);
    event.sensor_id = id;
    event.received_ms += 100;
    assert(store.ingest(event) == IngestResult::inserted);
  }
  assert(store.count() == kDeviceCapacity);
  assert(store.evictions() == 10);
  assert(store.at(kDeviceCapacity) == nullptr);
  assert(store.stale(kDeviceCapacity, 10000));

  const ScanProfile us = profile(Region::north_america);
  const ScanProfile eu = profile(Region::europe);
  assert(us.count > eu.count && us.count <= 16);
  assert(us.targets[0].center_hz == 315000000u);
  assert(eu.targets[0].center_hz == 433920000u);
  for (size_t i = 0; i < us.count; ++i) {
    assert(us.targets[i].center_hz > 0);
    assert(us.targets[i].dwell_seconds > 0);
  }

  store.clear();
  assert(store.count() == 0 && store.evictions() == 0 &&
         store.signals_unclassified() == 0 && store.decodes_unattributed() == 0);
  std::puts("IoT sensor discovery host tests PASS");
}

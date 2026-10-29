#include "drone_watch_rid_store.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace orcsdr::drone_watch::rid;

int main() {
  EventStore ring;
  ring.reset();
  StoredEvent event{};
  assert(ring.count() == 0);
  assert(!ring.newest(0, event));
  std::array<uint8_t, 25> id{};
  id[0] = 0x02;
  id[1] = 0x12;
  std::memcpy(id.data() + 2, "ORC-NODE-TEST", 13);
  const CaptureMeta node_a{Transport::bluetooth4, 12, 100};
  const CaptureMeta node_b{Transport::wifi_beacon, 20, 200};
  assert(ring.ingest(id.data(), id.size(), node_a) == ParseStatus::ok);
  assert(ring.ingest(id.data(), id.size(), node_b) == ParseStatus::ok);
  assert(ring.count() == 2);
  assert(ring.newest(0, event) && event.local_sequence == 2);
  assert(event.capture.source_id == 200 && !event.test_data);
  assert(ring.newest(1, event) && event.capture.source_id == 100);
  assert(ring.stats().packets_accepted == 2);
  assert(ring.stats().messages_accepted == 2);
  assert(ring.ingest(id.data(), 2, node_a) == ParseStatus::invalid_length);
  assert(ring.count() == 2 && ring.stats().packets_rejected == 1);
  assert(ring.newest(0, event) && event.capture.source_id == 200);

  // A test/fixture source MUST retain replay origin even with valid contents.
  CaptureMeta fixture{Transport::fixture, 24, 300};
  assert(ring.ingest(id.data(), id.size(), fixture) == ParseStatus::ok);
  assert(ring.newest(0, event) && event.test_data);
  // A BLE capture file replay is still TEST DATA, even though its original
  // physical transport was BLE4 rather than the synthetic fixture transport.
  CaptureMeta replayed_ble{Transport::bluetooth4, 25, 100, true};
  assert(ring.ingest(id.data(), id.size(), replayed_ble) == ParseStatus::ok);
  assert(ring.newest(0, event) && event.test_data);
  assert(event.capture.transport == Transport::bluetooth4);
  assert(event.capture.source_id == 300);

  // Ring wrap must preserve newest order, local sequence, and lost event count.
  for (uint32_t i = 0; i < 40; ++i) {
    CaptureMeta src{Transport::bluetooth5, 100 + i, 1000 + i};
    assert(ring.ingest(id.data(), id.size(), src) == ParseStatus::ok);
  }
  assert(ring.count() == EventStore::kCapacity);
  assert(ring.stats().events_overwritten == 12);
  assert(ring.stats().messages_accepted == 44);
  assert(ring.newest(0, event));
  assert(event.capture.source_id == 1039);
  assert(event.local_sequence == 44);
  assert(!ring.newest(EventStore::kCapacity, event));
  ring.reset();
  assert(ring.count() == 0 && ring.stats().events_overwritten == 0);
  assert(!ring.newest(0, event));
  std::cout << "Drone Watch RID store PASS: 2 independent sources, replay isolation, invalid packets, bounded history\n";
}

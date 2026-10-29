#pragma once
// Bounded application-message ingress. A future VERIFIED acquisition adapter may
// pass advertising bytes here from a non-UI task via a bounded handoff queue.
// This object is single-owner and has no ESP-Hosted, M5GFX or radio dependencies.
#include "drone_watch_ble4.hpp"
#include "drone_watch_rid_store.hpp"
namespace orcsdr::drone_watch::rid {
enum class Ble4IngressResult : uint8_t {
  decoded, unrelated_advertisement, malformed_advertisement,
  invalid_remote_id, wrong_transport
};
struct Ble4IngressCounters {
  uint32_t reports_received = 0;
  uint32_t decoded_reports = 0;      // one good 25-byte message per legacy AD
  uint32_t unrelated_reports = 0;
  uint32_t malformed_advertisements = 0;
  uint32_t invalid_remote_id = 0;
  uint32_t wrong_transport = 0;
};
class Ble4Ingress {
 public:
  void reset() { store_.reset(); counters_ = {}; }
  Ble4IngressResult accept(const uint8_t* ad, size_t len, CaptureMeta capture);
  const EventStore& observations() const { return store_; }
  const Ble4IngressCounters& counters() const { return counters_; }
 private:
  EventStore store_{};
  Ble4IngressCounters counters_{};
};
}

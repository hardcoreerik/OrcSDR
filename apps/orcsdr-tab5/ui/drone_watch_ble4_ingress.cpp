#include "drone_watch_ble4_ingress.hpp"
namespace orcsdr::drone_watch::rid {
Ble4IngressResult Ble4Ingress::accept(const uint8_t* ad, size_t len, CaptureMeta capture) {
  ++counters_.reports_received;
  // The transport is declared by the owning acquisition adapter, not inferred
  // from an arbitrary buffer or an ESP-Hosted Wi-Fi link.
  if (capture.transport != Transport::bluetooth4) {
    ++counters_.wrong_transport;
    return Ble4IngressResult::wrong_transport;
  }
  Ble4Message msg{};
  const Ble4FrameStatus envelope = extract_ble4_advertising_data(ad, len, msg);
  if (envelope == Ble4FrameStatus::no_remote_id) {
    ++counters_.unrelated_reports;
    return Ble4IngressResult::unrelated_advertisement;
  }
  if (envelope != Ble4FrameStatus::matched) {
    ++counters_.malformed_advertisements;
    return Ble4IngressResult::malformed_advertisement;
  }
  capture.advertising_counter_present = true;
  capture.advertising_counter = msg.advertising_counter;
  const ParseStatus parsed = store_.ingest(msg.encoded, kMessageSize, capture);
  if (parsed != ParseStatus::ok) {
    ++counters_.invalid_remote_id;
    return Ble4IngressResult::invalid_remote_id;
  }
  ++counters_.decoded_reports;
  return Ble4IngressResult::decoded;
}
}

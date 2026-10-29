#pragma once
// Bounded, allocation-free per-packet Remote ID observation history.
// Read/write from one owning app/service task; network/C6 ISR must hand off via
// a bounded queue. This is NOT a radio acquisition driver or aircraft tracker.
#include "drone_watch_rid.hpp"
#include <cstddef>
#include <cstdint>

namespace orcsdr::drone_watch::rid {
struct StoredEvent {
  uint64_t local_sequence = 0;    // local ring sequence, not sender identity
  CaptureMeta capture{};
  Observation observation{};
  bool test_data = false;
};
struct StoreStats {
  uint64_t packets_accepted = 0;
  uint64_t packets_rejected = 0;
  uint64_t messages_accepted = 0;
  uint64_t events_overwritten = 0; // bounded local history; never claim lossless
  ParseStatus last_status = ParseStatus::ok;
};

// Keep RAM modest on ESP32-P4; this is metadata, not raw packets or IQ.
class EventStore {
 public:
  static constexpr size_t kCapacity = 32;
  void reset();
  ParseStatus ingest(const uint8_t* bytes, size_t len, const CaptureMeta& meta);
  bool newest(size_t offset, StoredEvent& event) const;
  size_t count() const { return count_; }
  const StoreStats& stats() const { return stats_; }
 private:
  StoredEvent events_[kCapacity]{};
  size_t next_ = 0, count_ = 0;
  uint64_t sequence_ = 0;
  StoreStats stats_{};
};
}  // namespace orcsdr::drone_watch::rid

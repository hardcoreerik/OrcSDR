#include "drone_watch_rid_store.hpp"
namespace orcsdr::drone_watch::rid {
void EventStore::reset() {
  // Does not write SD or touch radios.
  // Clear retained IDs/locations on explicit reset, not just indices.
  for (auto& event : events_) event = StoredEvent{};
  next_ = 0;
  count_ = 0;
  sequence_ = 0;
  stats_ = StoreStats{};
}
ParseStatus EventStore::ingest(const uint8_t* bytes, size_t len, const CaptureMeta& meta) {
  Batch batch{};
  const ParseStatus status = decode(bytes, len, meta, batch);
  stats_.last_status = status;
  if (status != ParseStatus::ok) {
    ++stats_.packets_rejected;
    return status;
  }
  ++stats_.packets_accepted;
  for (uint8_t i = 0; i < batch.count; ++i) {
    if (count_ == kCapacity) ++stats_.events_overwritten;
    else ++count_;
    StoredEvent& dst = events_[next_];
    dst.local_sequence = ++sequence_;
    dst.capture = batch.capture;
    dst.observation = batch.messages[i];
    dst.test_data = batch.test_data;
    next_ = (next_ + 1) % kCapacity;
    ++stats_.messages_accepted;
  }
  return status;
}
bool EventStore::newest(size_t offset, StoredEvent& event) const {
  if (offset >= count_) return false;
  const size_t i = (next_ + kCapacity - 1 - offset) % kCapacity;
  event = events_[i];
  return true;
}
}  // namespace orcsdr::drone_watch::rid

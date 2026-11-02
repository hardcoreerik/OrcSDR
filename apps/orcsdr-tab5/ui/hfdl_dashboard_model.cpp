#include "hfdl_dashboard_model.hpp"

#include <cstring>

namespace orcsdr::hfdl_ui {

Model::Model() { reset(); }

void Model::reset() {
  live_ = {};
  live_.next_seq = 1;
  queue_head_.store(0);
  queue_tail_.store(0);
  receiver_state_.store(static_cast<uint8_t>(ReceiverState::idle));
  message_offset_ = 0;
  messages_revision_ = 1;
  revision_ = 0;
  dirty_ = true;
}

bool Model::post_message(const MessageRecord& msg) {
  const uint32_t head = queue_head_.load(std::memory_order_relaxed);
  const uint32_t next = (head + 1) % kQueueCapacity;
  if (next == queue_tail_.load(std::memory_order_acquire)) return false; // full
  queue_[head].msg = msg;
  queue_head_.store(next, std::memory_order_release);
  return true;
}

void Model::post_receiver_state(ReceiverState state) {
  receiver_state_.store(static_cast<uint8_t>(state), std::memory_order_relaxed);
  dirty_ = true;
}

void Model::service() {
  uint32_t tail = queue_tail_.load(std::memory_order_relaxed);
  const uint32_t head = queue_head_.load(std::memory_order_acquire);
  while (tail != head) {
    store(live_, queue_[tail].msg);
    tail = (tail + 1) % kQueueCapacity;
  }
  queue_tail_.store(tail, std::memory_order_release);
  if (dirty_) rebuild();
}

void Model::store(Ring& ring, const MessageRecord& msg) {
  MessageRecord& slot = ring.records[ring.head];
  slot = msg;
  slot.seq = ring.next_seq++;
  slot.source = SourceKind::live;
  ring.head = (ring.head + 1) % kHistoryCapacity;
  if (ring.held < kHistoryCapacity) ++ring.held;
  ++ring.total;
  dirty_ = true;
  ++messages_revision_;
}

void Model::rebuild() {
  snapshot_.source = SourceKind::live;
  snapshot_.receiver = static_cast<ReceiverState>(receiver_state_.load(std::memory_order_relaxed));
  snapshot_.messages_held = live_.held;
  snapshot_.messages_total = live_.total;
  snapshot_.messages_revision = messages_revision_;
  snapshot_.message_offset = message_offset_;

  const size_t available = live_.held;
  const size_t start = (live_.head + kHistoryCapacity - available + message_offset_) % kHistoryCapacity;
  size_t count = 0;
  for (size_t i = 0; i < kMessageRows && (message_offset_ + i) < available; ++i) {
    const size_t idx = (start + i) % kHistoryCapacity;
    snapshot_.messages[count++] = live_.records[idx];
  }
  snapshot_.message_count = count;
  snapshot_.revision = ++revision_;
  dirty_ = false;
}

const Snapshot& Model::snapshot() {
  if (dirty_) rebuild();
  return snapshot_;
}

}  // namespace orcsdr::hfdl_ui

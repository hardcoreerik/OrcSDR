#pragma once
// HFDL receive-only UI model. Fixed memory, SPSC event queue, no dynamic allocation.
// The producer owns queue writes and atomic receiver state; the UI task owns history
// and snapshots. reset() must only be called when the producer is stopped.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace orcsdr::hfdl_ui {
constexpr size_t kMessageRows=8;
constexpr size_t kHistoryCapacity=24;
constexpr size_t kQueueCapacity=8;        // eight usable slots
constexpr size_t kPreviewChars=48;
constexpr size_t kMaxTextBytes=128;
enum class SourceKind:uint8_t { live,demo };
enum class Integrity:uint8_t { crc_ok,crc_corrected };
enum class ReceiverState:uint8_t { idle,running,error };
struct MessageRecord {
  uint32_t seq=0;
  SourceKind source=SourceKind::live;
  Integrity integrity=Integrity::crc_ok;
  uint8_t corrected_bits=0;
  uint32_t frequency_hz=0;
  uint32_t uptime_ms=0;
  uint16_t text_length=0;
  char mode=0;
  char label[3]{};
  char tail[7]{};
  char preview[kPreviewChars]{};
  char text[kMaxTextBytes]{};
};
struct Snapshot {
  uint32_t revision=0, messages_revision=0;
  SourceKind source=SourceKind::live;
  ReceiverState receiver=ReceiverState::idle;
  uint32_t messages_held=0,messages_total=0,queue_drops=0;
  size_t message_offset=0,message_count=0;
  MessageRecord messages[kMessageRows]{};
};
class Model {
 public:
  Model();
  void reset();                                 // stop producer before reset
  bool post_message(const MessageRecord& msg);  // one producer, one consumer
  void post_receiver_state(ReceiverState state);
  void service();                               // UI thread only
  const Snapshot& snapshot();                   // UI thread only
  bool find_message(uint32_t seq, MessageRecord* out) const;  // UI only
  bool set_message_offset(size_t offset);       // UI only; newest first
  size_t message_offset() const {return message_offset_;}
  uint32_t live_ingested() const {return live_.total;}
  uint32_t queue_drops() const {return queue_drops_.load(std::memory_order_relaxed);}
 private:
  struct Ring {
    MessageRecord records[kHistoryCapacity]{};
    uint32_t next_seq=1,total=0;
    size_t held=0,head=0;
  };
  void store(const MessageRecord& msg);
  void rebuild();
  // A spare physical cell permits the SPSC queue to hold 8 actual entries.
  MessageRecord queue_[kQueueCapacity+1]{};
  std::atomic<uint32_t> queue_head_{0},queue_tail_{0},queue_drops_{0};
  std::atomic<uint8_t> receiver_state_{static_cast<uint8_t>(ReceiverState::idle)};
  Ring live_{};
  Snapshot snapshot_{};
  size_t message_offset_=0;
  uint32_t messages_revision_=1,revision_=0;
  bool dirty_=true;
};
}  // namespace orcsdr::hfdl_ui

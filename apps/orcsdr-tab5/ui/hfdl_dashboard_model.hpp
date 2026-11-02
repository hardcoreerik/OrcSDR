#pragma once
// HFDL dashboard data model (ESP32-P4 oriented).
// Allocation-free after construction. Bounded rings. No M5GFX / ESP-IDF dependency.
// Decoder posts via lock-free SPSC; UI task drains into Snapshot.
// Designed for tight PSRAM/internal RAM on ESP32-P4.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace orcsdr::hfdl_ui {

// Tight capacities for P4 memory budget.
constexpr size_t kMessageRows = 8;          // on-screen rows
constexpr size_t kHistoryCapacity = 24;     // ring size (smaller than ACARS)
constexpr size_t kQueueCapacity = 8;        // decoder -> UI events
constexpr size_t kPreviewChars = 48;        // short preview line
constexpr size_t kMaxTextBytes = 128;       // HFDL payload bound (typical ACARS block)

enum class SourceKind : uint8_t { live, demo };
enum class Integrity : uint8_t { crc_ok, crc_corrected };
enum class ReceiverState : uint8_t { idle, running, error };

struct MessageRecord {
  uint32_t seq = 0;
  SourceKind source = SourceKind::live;
  Integrity integrity = Integrity::crc_ok;
  uint8_t corrected_bits = 0;
  uint32_t frequency_hz = 0;
  uint32_t uptime_ms = 0;
  uint16_t text_length = 0;
  char mode = 0;
  char label[3]{};
  char tail[7]{};                 // short tail
  char preview[kPreviewChars]{};
  char text[kMaxTextBytes]{};
};

struct Snapshot {
  uint32_t revision = 0;
  uint32_t messages_revision = 0;
  SourceKind source = SourceKind::live;
  ReceiverState receiver = ReceiverState::idle;
  uint32_t messages_held = 0;
  uint32_t messages_total = 0;
  size_t message_offset = 0;
  size_t message_count = 0;
  MessageRecord messages[kMessageRows]{};
};

class Model {
 public:
  Model();
  void reset();

  // Producer (decoder task). Never blocks, no alloc.
  bool post_message(const MessageRecord& msg);
  void post_receiver_state(ReceiverState state);

  // UI task.
  void service();
  const Snapshot& snapshot();

  uint32_t live_ingested() const { return live_.total; }

 private:
  struct Event {
    MessageRecord msg{};
  };
  struct Ring {
    MessageRecord records[kHistoryCapacity]{};
    uint32_t next_seq = 1;
    uint32_t total = 0;
    size_t held = 0;
    size_t head = 0;
  };

  void store(Ring& ring, const MessageRecord& msg);
  void rebuild();

  Event queue_[kQueueCapacity]{};
  std::atomic<uint32_t> queue_head_{0};
  std::atomic<uint32_t> queue_tail_{0};
  std::atomic<uint8_t> receiver_state_{static_cast<uint8_t>(ReceiverState::idle)};

  Ring live_{};
  Snapshot snapshot_{};
  size_t message_offset_ = 0;
  uint32_t messages_revision_ = 1;
  uint32_t revision_ = 0;
  bool dirty_ = true;
};

}  // namespace orcsdr::hfdl_ui

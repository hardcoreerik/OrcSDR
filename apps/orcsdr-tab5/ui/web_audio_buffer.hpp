#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace orcsdr::web_console {

// Bounded shared PCM history, not a consuming queue. The owner must serialize
// append/read/reset; producers must use try-acquire and count contention loss.
// No socket work may run while that ownership is held. Place in PSRAM on Tab5.
class AudioBuffer {
 public:
  static constexpr size_t capacity = 32768;
  struct Cursor {
    uint64_t position;
    uint32_t generation;
  };
  struct ReadResult {
    size_t count = 0;
    uint64_t position = 0;
    uint64_t dropped = 0;
    size_t remaining = 0;
    uint32_t generation = 0;
    bool discontinuity = false;
  };

  Cursor subscribe() const { return {written_, generation_}; }

  void reset() {
    oldest_ = written_;
    ++generation_;
  }

  void append(const int16_t* samples, size_t count) {
    if (!samples || !count) return;
    const size_t retained = std::min(count, capacity);
    const size_t skipped = count - retained;
    const size_t offset = static_cast<size_t>((written_ + skipped) % capacity);
    const size_t first = std::min(retained, capacity - offset);
    std::memcpy(samples_ + offset, samples + skipped, first * sizeof(int16_t));
    std::memcpy(samples_, samples + skipped + first,
                (retained - first) * sizeof(int16_t));
    written_ += count;
    if (written_ - oldest_ > capacity) oldest_ = written_ - capacity;
  }

  ReadResult read(Cursor& cursor, int16_t* output, size_t limit) const {
    ReadResult result{};
    result.generation = generation_;
    if (!output || !limit) return result;
    if (cursor.generation != generation_) {
      cursor = {oldest_, generation_};
      result.discontinuity = true;
    }
    if (cursor.position < oldest_) {
      result.dropped = oldest_ - cursor.position;
      cursor.position = oldest_;
      result.discontinuity = true;
    }
    result.position = cursor.position;
    result.count = static_cast<size_t>(
        std::min<uint64_t>(limit, written_ - cursor.position));
    const size_t offset = static_cast<size_t>(cursor.position % capacity);
    const size_t first = std::min(result.count, capacity - offset);
    std::memcpy(output, samples_ + offset, first * sizeof(int16_t));
    std::memcpy(output + first, samples_, (result.count - first) * sizeof(int16_t));
    cursor.position += result.count;
    result.remaining = static_cast<size_t>(written_ - cursor.position);
    return result;
  }

 private:
  int16_t samples_[capacity]{};
  uint64_t written_ = 0;
  uint64_t oldest_ = 0;
  uint32_t generation_ = 1;
};

}  // namespace orcsdr::web_console

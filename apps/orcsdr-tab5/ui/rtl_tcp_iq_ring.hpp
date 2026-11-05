#pragma once

// Single-producer / single-consumer byte ring between the driver's IQ callback
// (producer) and the rtl_tcp server task (consumer). Header-only and free of
// RTOS calls, so it is stress-tested on the host.
//
// Blocks are written whole or not at all, so a full ring costs a visible,
// counted loss and never splits an I/Q pair. Reads return an even byte count.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace orcsdr::rtl_tcp {

class IqRing {
 public:
  // `storage` must outlive the ring; `capacity` should be a multiple of 2.
  void init(uint8_t* storage, size_t capacity) {
    storage_ = storage;
    capacity_ = capacity & ~static_cast<size_t>(1);
    reset();
  }
  // Consumer side, only while the producer is not running.
  void reset() {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
    overrun_bytes_.store(0, std::memory_order_relaxed);
  }

  // Producer. All or nothing; false means the block was lost and counted.
  bool write_block(const uint8_t* data, size_t bytes) {
    if (capacity_ == 0 || bytes == 0 || (bytes & 1u) != 0) return false;
    const uint64_t head = head_.load(std::memory_order_relaxed);
    const uint64_t tail = tail_.load(std::memory_order_acquire);
    if (bytes > capacity_ - static_cast<size_t>(head - tail)) {
      overrun_bytes_.fetch_add(bytes, std::memory_order_relaxed);
      return false;
    }
    const size_t offset = static_cast<size_t>(head % capacity_);
    const size_t first = bytes < capacity_ - offset ? bytes : capacity_ - offset;
    std::memcpy(storage_ + offset, data, first);
    if (first < bytes) std::memcpy(storage_, data + first, bytes - first);
    head_.store(head + bytes, std::memory_order_release);
    return true;
  }

  // Consumer. Returns an even number of bytes, 0 when nothing is ready.
  size_t read(uint8_t* destination, size_t max_bytes) {
    const uint64_t tail = tail_.load(std::memory_order_relaxed);
    const uint64_t head = head_.load(std::memory_order_acquire);
    size_t n = static_cast<size_t>(head - tail);
    if (n > max_bytes) n = max_bytes;
    n &= ~static_cast<size_t>(1);
    if (n == 0) return 0;
    const size_t offset = static_cast<size_t>(tail % capacity_);
    const size_t first = n < capacity_ - offset ? n : capacity_ - offset;
    std::memcpy(destination, storage_ + offset, first);
    if (first < n) std::memcpy(destination + first, storage_, n - first);
    tail_.store(tail + n, std::memory_order_release);
    return n;
  }

  size_t available() const {
    return static_cast<size_t>(head_.load(std::memory_order_acquire) -
                               tail_.load(std::memory_order_acquire));
  }
  uint64_t overrun_bytes() const { return overrun_bytes_.load(std::memory_order_relaxed); }
  size_t capacity() const { return capacity_; }

 private:
  uint8_t* storage_ = nullptr;
  size_t capacity_ = 0;
  std::atomic<uint64_t> head_{0};
  std::atomic<uint64_t> tail_{0};
  std::atomic<uint64_t> overrun_bytes_{0};
};

}  // namespace orcsdr::rtl_tcp

// SPDX-License-Identifier: AGPL-3.0-or-later
// DMR device qualification: lock-free SPSC, bounded, copy-only IQ attachment.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace orcsdr::dmr::device_probe {
inline constexpr std::uint32_t kRequiredRateHz = 48000;
inline constexpr std::size_t kBlockSamples = 240; // 5 ms of 48 kS/s
inline constexpr std::size_t kQueueBlocks = 8;    // 40 ms maximum backlog
inline constexpr std::size_t kMaxOfferSamples = 960; // 20 ms atomically

enum class Offer : std::uint8_t { accepted, full, bad_argument, wrong_rate };

template <typename IQ, std::size_t Slots = kQueueBlocks,
          std::size_t BlockSamples = kBlockSamples>
class SpscHandoff final {
  static_assert(Slots > 1 && Slots < 65536 && BlockSamples > 0 && BlockSamples <= 65535,
                "bounded ring parameters");
  static_assert(std::is_trivially_copyable<IQ>::value, "IQ must be POD-like");
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "lock-free 32-bit atomic required");
 public:
  struct Block {
    std::uint32_t epoch = 0;
    std::uint32_t sequence = 0;
    std::uint16_t samples = 0;
    std::array<IQ, BlockSamples> iq{};
  };
  struct Counters {
    std::uint32_t accepted_samples = 0;
    std::uint32_t dropped_samples = 0;
    std::uint32_t accepted_blocks = 0;
    std::uint32_t dropped_blocks = 0;
    std::uint32_t rejected_inputs = 0;
    std::uint32_t max_queued_blocks = 0;
    std::uint32_t queued_blocks = 0;
  };
  // ONLY producer invokes offer. All-or-nothing for up to 20ms of caller-owned
  // immutable 48k CF32 IQ. No waits, heap, RTOS calls, I/O or retune controls.
  Offer offer(const IQ* src, std::size_t count, std::uint32_t rate_hz,
              std::uint32_t epoch) noexcept {
    if (rate_hz != kRequiredRateHz) {
      ++rejected_inputs_; note_gap(count); return Offer::wrong_rate;
    }
    if (!src || !count || count > kMaxOfferSamples) {
      ++rejected_inputs_; note_gap(count); return Offer::bad_argument;
    }
    const auto need = static_cast<std::uint32_t>((count + BlockSamples - 1U) / BlockSamples);
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (need > Slots - static_cast<std::size_t>(head - tail)) {
      // Count skipped sequence numbers so the consumer can reset even if no
      // explicit discontinuity notification can enter the full queue.
      next_sequence_ += need;
      dropped_samples_.fetch_add(static_cast<std::uint32_t>(count), std::memory_order_relaxed);
      dropped_blocks_.fetch_add(need, std::memory_order_relaxed);
      return Offer::full;
    }
    std::size_t copied = 0;
    for (std::uint32_t i = 0; i < need; ++i) {
      auto& slot = blocks_[(head + i) % Slots];
      const auto n = (count - copied < BlockSamples) ? count - copied : BlockSamples;
      slot.epoch = epoch;
      slot.sequence = ++next_sequence_;
      slot.samples = static_cast<std::uint16_t>(n);
      std::memcpy(slot.iq.data(), src + copied, n * sizeof(IQ));
      copied += n;
    }
    accepted_samples_.fetch_add(static_cast<std::uint32_t>(count), std::memory_order_relaxed);
    accepted_blocks_.fetch_add(need, std::memory_order_relaxed);
    const auto now = static_cast<std::uint32_t>(head + need - tail);
    auto previous = max_depth_.load(std::memory_order_relaxed);
    while (previous < now && !max_depth_.compare_exchange_weak(
             previous, now, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    head_.store(head + need, std::memory_order_release);
    return Offer::accepted;
  }
  // Producer-only: explicitly account for a known gap in the existing channel
  // stream, including upstream pipeline loss. The *next* received frame then
  // forces Receiver::discontinuity() before any new IQ is decoded.
  void note_gap(std::size_t lost_samples) noexcept {
    if (!lost_samples) return;
    // At least one sequence number is lost, no matter how large the gap.
    const auto chunks = lost_samples / BlockSamples + (lost_samples % BlockSamples != 0U);
    const auto skipped = static_cast<std::uint32_t>(
        chunks > 0xffffffffULL ? 0xffffffffULL : chunks);
    next_sequence_ += skipped;
    const auto lost = static_cast<std::uint32_t>(
        lost_samples > 0xffffffffULL ? 0xffffffffULL : lost_samples);
    dropped_samples_.fetch_add(lost, std::memory_order_relaxed);
    dropped_blocks_.fetch_add(skipped, std::memory_order_relaxed);
  }
  // ONLY consumer invokes peek/release. Pointer remains valid until release;
  // release must be called once after processing, not from a callback.
  const Block* peek() const noexcept {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return nullptr;
    return &blocks_[tail % Slots];
  }
  void release() noexcept {
    if (peek()) tail_.fetch_add(1U, std::memory_order_release);
  }
  Counters counters() const noexcept {
    return {accepted_samples_.load(std::memory_order_relaxed),
            dropped_samples_.load(std::memory_order_relaxed),
            accepted_blocks_.load(std::memory_order_relaxed),
            dropped_blocks_.load(std::memory_order_relaxed),
            rejected_inputs_.load(std::memory_order_relaxed),
            max_depth_.load(std::memory_order_relaxed),
            static_cast<std::uint32_t>(head_.load(std::memory_order_acquire) -
                                       tail_.load(std::memory_order_acquire))};
  }
  [[nodiscard]] static constexpr std::size_t storage_bytes() noexcept { return sizeof(SpscHandoff); }
 private:
  std::array<Block, Slots> blocks_{};
  alignas(64) std::atomic<std::uint32_t> head_{0};
  alignas(64) std::atomic<std::uint32_t> tail_{0};
  std::uint32_t next_sequence_ = 0; // producer only; wrapping sequence allowed
  std::atomic<std::uint32_t> accepted_samples_{0};
  std::atomic<std::uint32_t> dropped_samples_{0};
  std::atomic<std::uint32_t> accepted_blocks_{0};
  std::atomic<std::uint32_t> dropped_blocks_{0};
  std::atomic<std::uint32_t> rejected_inputs_{0};
  std::atomic<std::uint32_t> max_depth_{0};
};

template <typename IQ, typename Receiver,
          std::size_t Slots = kQueueBlocks, std::size_t BlockSamples = kBlockSamples>
class Attachment final {
 public:
  using Queue = SpscHandoff<IQ, Slots, BlockSamples>;
  // Only the existing radio service may call this, and only when opted in.
  Offer offer(const IQ* iq, std::size_t count, std::uint32_t rate_hz,
              std::uint32_t receiver_epoch) noexcept {
    return queue_.offer(iq, count, rate_hz, receiver_epoch);
  }
  // ONLY one independent worker calls drain_one; never on the USB callback.
  bool drain_one() noexcept {
    const auto* block = queue_.peek();
    if (!block) return false;
    if (seen_ && (block->sequence != last_sequence_ + 1U || block->epoch != last_epoch_ || must_reset_)) {
      receiver_.discontinuity();
      ++discontinuities_;
      must_reset_ = false;
    }
    const auto result = receiver_.process(block->iq.data(), block->samples);
    if (!result.ok || result.consumed != block->samples) {
      ++processing_errors_;
      must_reset_ = true; // next frame cannot be joined to a damaged one
    }
    processed_samples_ += static_cast<std::uint32_t>(result.consumed);
    last_sequence_ = block->sequence;
    last_epoch_ = block->epoch;
    seen_ = true;
    queue_.release();
    return true;
  }
  void note_gap(std::size_t lost_samples) noexcept { queue_.note_gap(lost_samples); }
  // No producer resets receiver state; a hardware owner increments the epoch
  // on start/retune/stop/reconnect and only the consumer performs reset.
  [[nodiscard]] const Receiver& receiver() const noexcept { return receiver_; }
  [[nodiscard]] Receiver& worker_receiver() noexcept { return receiver_; }
  [[nodiscard]] const Queue& queue() const noexcept { return queue_; }
  [[nodiscard]] std::uint64_t processed_samples() const noexcept { return processed_samples_; }
  [[nodiscard]] std::uint32_t discontinuities() const noexcept { return discontinuities_; }
  [[nodiscard]] std::uint32_t processing_errors() const noexcept { return processing_errors_; }
  [[nodiscard]] static constexpr std::size_t storage_bytes() noexcept { return sizeof(Attachment); }
 private:
  Queue queue_{};
  Receiver receiver_{};
  std::uint64_t processed_samples_ = 0;
  std::uint32_t last_sequence_ = 0;
  std::uint32_t last_epoch_ = 0;
  std::uint32_t discontinuities_ = 0;
  std::uint32_t processing_errors_ = 0;
  bool seen_ = false;
  bool must_reset_ = false;
};
} // namespace orcsdr::dmr::device_probe

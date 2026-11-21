// Bounded cross-thread CU8 IQ handoff; receiver ownership remains with producer.
// This is a transport foundation, not an assertion of RF sample continuity.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace orc::linux_sdr {
struct IqMetadata {
  std::uint32_t frequency_hz=0;
  std::uint32_t sample_rate_sps=0;
  std::uint64_t receiver_generation=0;
  std::uint64_t first_sample_index=0; // source-relative complex index, not RF-hardware sample time
  std::uint64_t monotonic_ns=0;
  std::uint64_t dropped_samples_before=0; // exact known application queue drops
  bool discontinuity=false;              // retune, overrun or unknown transport gap
};
struct IqBlock {
  std::vector<std::uint8_t> cu8;
  IqMetadata meta{};
};
class BoundedIqQueue {
 public:
  BoundedIqQueue(std::size_t blocks, std::size_t max_bytes)
      : slots_(blocks), max_bytes_(max_bytes) {
    if (!blocks || max_bytes<2 || (max_bytes&1u))
      throw std::invalid_argument("IQ queue requires positive capacity and even CU8 byte limit");
    for (auto& slot:slots_) slot.cu8.resize(max_bytes);
  }
  BoundedIqQueue(const BoundedIqQueue&)=delete;
  BoundedIqQueue& operator=(const BoundedIqQueue&)=delete;
  bool try_push(const std::uint8_t* data,std::size_t bytes,IqMetadata meta) {
    if (!data || !bytes || (bytes&1u) || bytes>max_bytes_)
      throw std::invalid_argument("invalid CU8 IQ block");
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_==slots_.size()) {
      const auto lost=static_cast<std::uint64_t>(bytes/2);
      dropped_total_+=lost;
      pending_dropped_+=lost;
      pending_gap_=true;
      return false;
    }
    auto& slot=slots_[tail_];
    std::memcpy(slot.cu8.data(),data,bytes);
    slot.length=bytes;
    meta.dropped_samples_before=pending_dropped_;
    meta.discontinuity=meta.discontinuity||pending_gap_;
    slot.meta=meta;
    pending_dropped_=0;
    pending_gap_=false;
    tail_=(tail_+1)%slots_.size();
    ++count_;
    return true;
  }
  bool try_pop(IqBlock& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!count_) return false;
    const auto& slot=slots_[head_];
    output.cu8.assign(slot.cu8.begin(),slot.cu8.begin()+slot.length);
    output.meta=slot.meta;
    head_=(head_+1)%slots_.size();
    --count_;
    return true;
  }
  // A retune, unplug, USB read error or receiver restart invalidates all
  // queued blocks. Unknown driver-side loss is not invented as a sample count.
  void flush_discontinuity() {
    std::lock_guard<std::mutex> lock(mutex_);
    while(count_) {
      const auto lost=static_cast<std::uint64_t>(slots_[head_].length/2);
      dropped_total_+=lost;
      pending_dropped_+=lost;
      head_=(head_+1)%slots_.size();
      --count_;
    }
    tail_=head_;
    pending_gap_=true;
  }
  std::uint64_t dropped_samples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_total_;
  }
  std::size_t queued_blocks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
  }
  std::size_t capacity_blocks() const { return slots_.size(); }
 private:
  struct Slot { std::vector<std::uint8_t> cu8; std::size_t length=0; IqMetadata meta{}; };
  mutable std::mutex mutex_;
  std::vector<Slot> slots_;
  std::size_t max_bytes_=0,head_=0,tail_=0,count_=0;
  std::uint64_t dropped_total_=0,pending_dropped_=0;
  bool pending_gap_=false;
};
}

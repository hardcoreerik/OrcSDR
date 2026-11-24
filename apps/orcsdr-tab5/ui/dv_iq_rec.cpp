#include "dv_iq_rec.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace orcsdr::dv_iq_rec {

bool Recorder::begin() {
  if (ring_) return true;
#if defined(ESP_PLATFORM)
  ring_ = static_cast<uint8_t*>(heap_caps_malloc(kRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  ring_ = static_cast<uint8_t*>(std::malloc(kRingBytes));
#endif
  wr_ = 0;
  return ring_ != nullptr;
}

void Recorder::end() {
  enabled_.store(false, std::memory_order_relaxed);
  if (state_.load() == kSaving) { free_pending_ = true; return; }    // the card is still reading it
  state_.store(kIdle);
  if (!ring_) return;
#if defined(ESP_PLATFORM)
  heap_caps_free(ring_);
#else
  std::free(ring_);
#endif
  ring_ = nullptr;
  free_pending_ = false;
}

void Recorder::set_enabled(bool on) {
  if (on && !ring_ && !begin()) return;
  enabled_.store(on, std::memory_order_relaxed);
  if (!on && state_.load() != kSaving) { state_.store(kIdle); }
}

void Recorder::reset() { wr_ = 0; if (state_.load() == kArmed) state_.store(kIdle); }

void Recorder::feed(const uint8_t* cu8, size_t bytes) {
  if (!ring_ || !enabled_.load(std::memory_order_relaxed)) return;
  const uint8_t st = state_.load(std::memory_order_acquire);
  if (st == kFrozen || st == kSaving) return;
  size_t pos = static_cast<size_t>(wr_ % kRingBytes);
  size_t left = bytes;
  while (left) {
    const size_t n = std::min(left, kRingBytes - pos);
    std::memcpy(ring_ + pos, cu8 + (bytes - left), n);
    pos = (pos + n) % kRingBytes;
    left -= n;
  }
  wr_ += bytes;
}

void Recorder::trigger(uint32_t now_ms, uint32_t channel_hz, uint32_t epoch) {
  if (!ring_ || !enabled_.load(std::memory_order_relaxed)) return;
  ++stats_.triggers;
  if (state_.load() != kIdle) { ++stats_.skipped_busy; return; }
  if (files_ >= kMaxFiles) { ++stats_.skipped_limit; return; }
  if (cooldown_until_ && static_cast<int32_t>(now_ms - cooldown_until_) < 0) { ++stats_.skipped_cooldown; return; }
  if (wr_ < 2 * kBytesPerSec) return;                    // no continuous pre-roll yet
  channel_hz_ = channel_hz; epoch_ = epoch;
  due_ms_ = now_ms + kPostSeconds * 1000u;
  state_.store(kArmed, std::memory_order_release);
}

void Recorder::service(uint32_t now_ms) {
  if (state_.load() == kArmed && static_cast<int32_t>(now_ms - due_ms_) >= 0) {
    frozen_bytes_ = static_cast<size_t>(std::min<uint64_t>(wr_, kRingBytes));
    state_.store(kFrozen, std::memory_order_release);   // from here feed() ignores new blocks
  }
}

bool Recorder::ready(const uint8_t** data, size_t* bytes, char* name, size_t name_size) {
  uint8_t st = state_.load(std::memory_order_acquire);
  if (st != kFrozen || !ring_) return false;       // idempotent until saving() is called
  if (!linear_ && frozen_bytes_ == kRingBytes) {          // full ring: rotate once so the oldest byte comes first
    const size_t start = static_cast<size_t>(wr_ % kRingBytes);
    if (start) std::rotate(ring_, ring_ + start, ring_ + kRingBytes);
    wr_ = kRingBytes;                                     // now linear
  }
  linear_ = true;
  if (data) *data = ring_;
  if (bytes) *bytes = frozen_bytes_;
  if (name && name_size) std::snprintf(name, name_size, "/orcsdr/dviq_%lu_%lu.cu8", static_cast<unsigned long>(epoch_ ? epoch_ : files_ + 1),
                                       static_cast<unsigned long>(channel_hz_ / 1000u));
  return true;
}

void Recorder::release(bool saved, uint32_t now_ms) {
  if (saved) { ++stats_.saved; ++files_; } else ++stats_.failed;
  cooldown_until_ = now_ms + kCooldownMs;
  wr_ = 0; linear_ = false;                               // the next capture starts with fresh, continuous pre-roll
  state_.store(kIdle, std::memory_order_release);
  if (free_pending_) end();
}

}  // namespace orcsdr::dv_iq_rec

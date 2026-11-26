#include "dv_capture.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::dv_capture {
namespace {
std::atomic<Capture*> g_tap{nullptr};
std::atomic<bool> g_in_offer{false};
}  // namespace

bool Capture::start(const Config& c, uint32_t now_ms) noexcept {
  if (!mem_ || c.center_hz == 0 || c.rate == 0 || c.seconds == 0 || c.seconds > kMaxSeconds) return false;
  if (state() == State::capturing || state() == State::draining) return false;
  cfg_ = c;
  for (size_t i = 0; i < kSegs; ++i) { seg_state_[i].store(kFree, std::memory_order_relaxed); seg_len_[i] = 0; seg_index_[i] = 0; }
  fill_seg_ = write_seg_ = 0;
  next_index_ = written_ = failed_ = drop_events_ = 0;
  captured_ = dropped_ = stream_off_ = 0;
  wrong_rate_.store(0, std::memory_order_relaxed);
  for (auto& d : drops_) d = Drop{};
  start_ms_ = now_ms;
  writing_ = false;
  stop_requested_ = false;
  state_.store(State::capturing, std::memory_order_release);
  return true;
}

void Capture::stop() noexcept { stop_requested_ = true; }

void Capture::abort() noexcept {
  state_.store(State::idle, std::memory_order_release);
  for (uint32_t i = 0; i < 100000u && g_in_offer.load(std::memory_order_acquire); ++i) {}
  writing_ = false;
}

void Capture::offer(const uint8_t* cu8, size_t bytes, uint32_t rate) noexcept {
  if (state() != State::capturing || !mem_ || !cu8 || bytes == 0) return;
  g_in_offer.store(true, std::memory_order_release);
  if (state() != State::capturing) { g_in_offer.store(false, std::memory_order_release); return; }
  if (rate + cfg_.rate / 100u < cfg_.rate || rate > cfg_.rate + cfg_.rate / 100u) {      // another rate: not ours, never resampled
    wrong_rate_.fetch_add(1, std::memory_order_relaxed);
    g_in_offer.store(false, std::memory_order_release);
    return;
  }
  size_t left = bytes & ~static_cast<size_t>(1);                                          // whole I/Q pairs only
  while (left > 0) {
    const size_t s = fill_seg_;
    uint8_t st = seg_state_[s].load(std::memory_order_acquire);
    if (st == kFree) {
      seg_len_[s] = 0;
      seg_index_[s] = next_index_++;
      seg_state_[s].store(kFilling, std::memory_order_relaxed);
      st = kFilling;
    }
    if (st != kFilling) {                                                                  // every segment is waiting for the card: this block is lost
      if (drop_events_ == 0 || drops_[std::min<uint32_t>(drop_events_, kMaxDrops) - 1].offset + drops_[std::min<uint32_t>(drop_events_, kMaxDrops) - 1].bytes != stream_off_ ||
          drop_events_ > kMaxDrops) {
        if (drop_events_ < kMaxDrops) drops_[drop_events_] = {stream_off_, static_cast<uint32_t>(left)};
        ++drop_events_;
      } else {
        drops_[drop_events_ - 1].bytes += static_cast<uint32_t>(left);                     // contiguous with the previous gap
      }
      dropped_ += left;
      stream_off_ += left;
      break;
    }
    const size_t n = std::min<size_t>(left, kSegBytes - seg_len_[s]);
    std::memcpy(mem_ + s * kSegBytes + seg_len_[s], cu8 + (bytes & ~static_cast<size_t>(1)) - left, n);
    seg_len_[s] += static_cast<uint32_t>(n);
    captured_ += n;
    stream_off_ += n;
    left -= n;
    if (seg_len_[s] == kSegBytes) {
      seg_state_[s].store(kFull, std::memory_order_release);
      fill_seg_ = (s + 1) % kSegs;
    }
  }
  g_in_offer.store(false, std::memory_order_release);
}

void Capture::service(uint32_t now_ms) noexcept {
  if (state() == State::capturing && (stop_requested_ || now_ms - start_ms_ >= cfg_.seconds * 1000u)) {
    state_.store(State::draining, std::memory_order_release);
    for (uint32_t i = 0; i < 100000u && g_in_offer.load(std::memory_order_acquire); ++i) {}   // a block mid-copy finishes
    const size_t s = fill_seg_;
    if (seg_state_[s].load(std::memory_order_acquire) == kFilling) {
      if (seg_len_[s] > 0) seg_state_[s].store(kFull, std::memory_order_release);
      else seg_state_[s].store(kFree, std::memory_order_release);
    }
  }
  if (state() == State::draining && !writing_) {
    bool pending = false;
    for (size_t i = 0; i < kSegs; ++i) pending = pending || seg_state_[i].load(std::memory_order_acquire) != kFree;
    if (!pending) state_.store(State::done, std::memory_order_release);
  }
}

void Capture::name(char* out, size_t cap, uint32_t index) const noexcept {
  std::snprintf(out, cap, "/orcsdr/cap_%lu_%lukHz_%lukSps_%04lu.cu8", static_cast<unsigned long>(cfg_.utc), static_cast<unsigned long>(cfg_.center_hz / 1000u),
                static_cast<unsigned long>(cfg_.rate / 1000u), static_cast<unsigned long>(index));
}

bool Capture::next(const uint8_t** data, size_t* bytes, char* path, size_t path_cap) noexcept {
  if (writing_ || !mem_) return false;
  const size_t s = write_seg_;
  if (seg_state_[s].load(std::memory_order_acquire) != kFull) return false;
  *data = mem_ + s * kSegBytes;
  *bytes = seg_len_[s];
  name(path, path_cap, seg_index_[s]);
  return true;
}

void Capture::write_started() noexcept {
  seg_state_[write_seg_].store(kWriting, std::memory_order_release);
  writing_ = true;
}

void Capture::write_done(bool ok) noexcept {
  seg_state_[write_seg_].store(kFree, std::memory_order_release);
  write_seg_ = (write_seg_ + 1) % kSegs;
  if (ok) ++written_; else ++failed_;
  writing_ = false;
}

bool Capture::sidecar_path(char* out, size_t cap) const noexcept {
  return std::snprintf(out, cap, "/orcsdr/cap_%lu_%lukHz_%lukSps.json", static_cast<unsigned long>(cfg_.utc), static_cast<unsigned long>(cfg_.center_hz / 1000u),
                       static_cast<unsigned long>(cfg_.rate / 1000u)) < static_cast<int>(cap);
}

size_t Capture::sidecar(char* out, size_t cap) const noexcept {
  size_t w = 0;
  auto add = [&](const char* fmt, auto... a) {
    if (w >= cap) return;
    const int n = std::snprintf(out + w, cap - w, fmt, a...);
    if (n < 0 || static_cast<size_t>(n) >= cap - w) { w = cap; return; }
    w += static_cast<size_t>(n);
  };
  add("{\"format\":\"cu8\",\"iq\":\"interleaved I,Q, unsigned 8-bit, zero at 127.5\",\"source\":\"Tab5 receiver stream, as the decoders see it\","
      "\"center_hz\":%lu,\"sample_rate_sps\":%lu,\"utc_start\":%lu,\"seconds_requested\":%lu,\"bytes_captured\":%llu,\"bytes_dropped\":%llu,"
      "\"drop_events\":%lu,\"wrong_rate_blocks\":%lu,\"segment_bytes\":%lu,\"segments_written\":%lu,\"segments_failed\":%lu,"
      "\"files\":\"cap_%lu_%lukHz_%lukSps_NNNN.cu8, concatenate in order\",\"drops\":[",
      static_cast<unsigned long>(cfg_.center_hz), static_cast<unsigned long>(cfg_.rate), static_cast<unsigned long>(cfg_.utc),
      static_cast<unsigned long>(cfg_.seconds), static_cast<unsigned long long>(captured_), static_cast<unsigned long long>(dropped_),
      static_cast<unsigned long>(drop_events_), static_cast<unsigned long>(wrong_rate_.load(std::memory_order_relaxed)),
      static_cast<unsigned long>(kSegBytes), static_cast<unsigned long>(written_), static_cast<unsigned long>(failed_),
      static_cast<unsigned long>(cfg_.utc), static_cast<unsigned long>(cfg_.center_hz / 1000u), static_cast<unsigned long>(cfg_.rate / 1000u));
  const uint32_t n = std::min<uint32_t>(drop_events_, kMaxDrops);
  for (uint32_t i = 0; i < n; ++i)
    add("%s{\"stream_offset\":%llu,\"bytes\":%lu}", i ? "," : "", static_cast<unsigned long long>(drops_[i].offset), static_cast<unsigned long>(drops_[i].bytes));
  add("],\"drops_listed\":%lu}\n", static_cast<unsigned long>(n));
  return w >= cap ? 0 : w;
}

void install(Capture* c) noexcept { g_tap.store(c, std::memory_order_release); }
bool active() noexcept {
  Capture* c = g_tap.load(std::memory_order_acquire);
  return c && c->capturing();
}
void offer_iq(const uint8_t* cu8, size_t bytes, uint32_t rate) noexcept {
  Capture* c = g_tap.load(std::memory_order_acquire);
  if (c) c->offer(cu8, bytes, rate);
}

}  // namespace orcsdr::dv_capture

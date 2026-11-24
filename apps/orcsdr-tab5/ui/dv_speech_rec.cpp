#include "dv_speech_rec.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace orcsdr::dv_speech_rec {
namespace {
void* alloc(size_t bytes) {
#if defined(ESP_PLATFORM)
  return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::calloc(1, bytes);
#endif
}
void release_mem(void* p) {
#if defined(ESP_PLATFORM)
  heap_caps_free(p);
#else
  std::free(p);
#endif
}
void put32(uint8_t* d, uint32_t v) { for (int i = 0; i < 4; ++i) d[i] = static_cast<uint8_t>(v >> (8 * i)); }
void put16(uint8_t* d, uint16_t v) { d[0] = static_cast<uint8_t>(v); d[1] = static_cast<uint8_t>(v >> 8); }
}  // namespace

bool Recorder::begin() {
  if (buf_) return true;
  buf_ = static_cast<int16_t*>(alloc(kMaxSamples * sizeof(int16_t)));   // 1.9 MB
  return buf_ != nullptr;
}
void Recorder::end() {
  if (buf_) release_mem(buf_);
  buf_ = nullptr;
  active_ = pending_ = false;
  n_ = 0;
}
void Recorder::set_enabled(bool on) {
  if (on == enabled_) return;
  enabled_ = on;
  if (!on) { active_ = false; n_ = 0; }          // a call in progress is discarded, a finished one still gets saved
}

void Recorder::start(uint8_t slot, uint32_t serial, uint32_t src, uint32_t dst) {
  active_ = true; n_ = 0; speech_samples_ = 0; any_speech_ = false; truncated_ = false;
  slot_ = slot; serial_ = serial; src_ = src; dst_ = dst;
}

void Recorder::frame(uint8_t slot, uint32_t serial, uint32_t src, uint32_t dst, const int16_t pcm[160], bool silent, uint32_t now_ms) {
  if (!enabled_ || !buf_ || slot < 1 || slot > 2) return;     // slot 0 is the speaker-path test tone: never recorded
  if (active_ && (serial != serial_ || slot != slot_)) finalize(now_ms);
  if (!active_) {
    if (pending_) {                                           // the previous file is still waiting for the card
      if (!busy_counted_) { ++stats_.calls_dropped_busy; busy_counted_ = true; }
      return;
    }
    busy_counted_ = false;
    start(slot, serial, src, dst);
  }
  last_ms_ = now_ms;
  if (n_ + 160 > kMaxSamples) { truncated_ = true; return; }
  if (silent) std::memset(buf_ + n_, 0, 160 * sizeof(int16_t));
  else { std::memcpy(buf_ + n_, pcm, 160 * sizeof(int16_t)); speech_samples_ += 160; any_speech_ = true; }
  n_ += 160;
}

void Recorder::gap(size_t frames) {
  if (!active_ || !buf_) return;
  for (size_t i = 0; i < frames && n_ + 160 <= kMaxSamples; ++i) { std::memset(buf_ + n_, 0, 160 * sizeof(int16_t)); n_ += 160; }
}

void Recorder::service(uint32_t now_ms) {
  if (active_ && now_ms - last_ms_ > 2500u) finalize(now_ms);
}
void Recorder::flush() { if (active_) finalize(last_ms_); }

void Recorder::finalize(uint32_t) {
  active_ = false;
  if (!any_speech_ || speech_samples_ * 1000u / kRateHz < kMinSpeechMs || pending_) { ++stats_.calls_discarded; n_ = 0; return; }
  if (truncated_) ++stats_.calls_truncated;
  const uint32_t id = epoch_ ? epoch_ : ++seq_;
  std::snprintf(pend_name_, sizeof(pend_name_), "/orcsdr/dmr_%s%lu_ts%u_%lu.wav", epoch_ ? "" : "n", static_cast<unsigned long>(id),
                static_cast<unsigned>(slot_), static_cast<unsigned long>(src_));
  pend_n_ = n_;
  pending_ = true;
  n_ = 0;
}

bool Recorder::ready(const int16_t** pcm, size_t* samples, char* name, size_t name_size, uint8_t header[kHeaderBytes]) const {
  if (!pending_ || !buf_) return false;
  if (pcm) *pcm = buf_;
  if (samples) *samples = pend_n_;
  if (name && name_size) { std::strncpy(name, pend_name_, name_size - 1); name[name_size - 1] = 0; }
  if (header) {
    const uint32_t bytes = static_cast<uint32_t>(pend_n_ * 2);
    std::memcpy(header, "RIFF", 4); put32(header + 4, 36 + bytes); std::memcpy(header + 8, "WAVEfmt ", 8);
    put32(header + 16, 16); put16(header + 20, 1); put16(header + 22, 1); put32(header + 24, kRateHz); put32(header + 28, kRateHz * 2);
    put16(header + 32, 2); put16(header + 34, 16); std::memcpy(header + 36, "data", 4); put32(header + 40, bytes);
  }
  return true;
}

void Recorder::release(bool saved) {
  if (!pending_) return;
  pending_ = false;
  if (saved) { ++stats_.calls_saved; stats_.last_samples = static_cast<uint32_t>(pend_n_); }
  else ++stats_.calls_discarded;
}

}  // namespace orcsdr::dv_speech_rec

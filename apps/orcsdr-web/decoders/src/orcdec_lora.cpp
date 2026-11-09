// LoRa / Meshtastic adapter: OrcSDR's preamble detector and native decoder behind the orcdec ABI.
//
// Input is CU8 IQ at 960 kS/s centred on the channel (Meshtastic LongFast is SF11, 250 kHz). This is the capture pipeline of OrcSDR's
// main.cpp: a 250 ms pre-roll ring runs all the time; the real-time preamble detector watches the stream; when it fires, the pre-roll
// and the IQ that follows are captured until at least 2 s are held and no chirp activity has been seen for 333 ms (or 4 s is reached);
// then `lora_native::decode_capture` decodes the capture (several seconds of work on a P4: the feed that completes a capture does it
// and returns afterwards, so a node loses the IQ that arrives meanwhile, as the device does while its decode task runs).
//
// Events: "packet" (one per decoded Meshtastic frame), "capture" (one per capture decoded, with the decoder's counters), and the "stats"
// command's snapshot. Options: sf=7..12 (default 11), freq=HZ (reported only).
#include "orcdec_internal.hpp"

#include "lora_native_decoder.hpp"
#include "lora_preamble_detector.hpp"

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

#include <algorithm>
#include <cmath>

namespace orcsdr::rf_analysis {
// The decoder asks the analysis module to prepare the FFT tables it shares; this build has no analysis module and its own transform
// needs no shared table (see shim_portable/dsps_fft2r.h).
bool initialize_fft() { return true; }
}  // namespace orcsdr::rf_analysis

namespace {

using namespace orcsdr;

constexpr uint32_t kRate = 960000;
constexpr size_t kPreRollBytes = kRate / 2u;                 // 250 ms of CU8
constexpr size_t kMinCaptureBytes = kRate * 2u * 2u;         // 2 s
constexpr size_t kMaxCaptureBytes = kRate * 2u * 4u;         // 4 s
constexpr uint32_t kQuietSamples = kRate / 3u;               // 333 ms
constexpr size_t kCandidateAfterTrigger = kRate * 3u / 4u;   // samples

void* big_alloc(size_t bytes) {
#if defined(ESP_PLATFORM)
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
#else
  return malloc(bytes);
#endif
}
void big_free(void* p) {
#if defined(ESP_PLATFORM)
  heap_caps_free(p);
#else
  free(p);
#endif
}

struct Lora : orcdec {
  explicit Lora(const char* options) {
    sf_ = static_cast<uint8_t>(std::clamp(atoi(option(options, "sf", "11").c_str()), 7, 12));
    freq_ = static_cast<uint32_t>(strtoul(option(options, "freq", "0").c_str(), nullptr, 10));
    pre_ = static_cast<uint8_t*>(big_alloc(kPreRollBytes));
    cap_ = static_cast<uint8_t*>(big_alloc(kMaxCaptureBytes));
    ok_ = pre_ && cap_ && lora_native::initialize() && lora_preamble::configure(sf_);
    if (ok_) lora_preamble::reset();   // the detector is a module-wide singleton: a new decoder must not inherit the previous run's state
  }

  ~Lora() override {
    big_free(pre_);
    big_free(cap_);
  }

  void reset() override {
    lora_preamble::reset();
    pre_write_ = pre_fill_ = 0;
    pend_ = 0;
    cap_len_ = 0;
    capturing_ = false;
  }

  void pre_append(const uint8_t* iq, size_t bytes) {
    if (bytes >= kPreRollBytes) {
      memcpy(pre_, iq + bytes - kPreRollBytes, kPreRollBytes);
      pre_write_ = 0;
      pre_fill_ = kPreRollBytes;
      return;
    }
    const size_t first = std::min(bytes, kPreRollBytes - pre_write_);
    memcpy(pre_ + pre_write_, iq, first);
    if (first < bytes) memcpy(pre_, iq + first, bytes - first);
    pre_write_ = (pre_write_ + bytes) % kPreRollBytes;
    pre_fill_ = std::min(kPreRollBytes, pre_fill_ + bytes);
  }

  // the pre-roll in time order into the capture buffer; returns its length in bytes
  size_t pre_copy() {
    const size_t start = pre_fill_ == kPreRollBytes ? pre_write_ : 0;
    const size_t first = std::min(pre_fill_, kPreRollBytes - start);
    memcpy(cap_, pre_ + start, first);
    if (first < pre_fill_) memcpy(cap_ + first, pre_, pre_fill_ - first);
    return pre_fill_;
  }

  void decode_capture() {
    capturing_ = false;
    static lora_native::Packet packets[lora_native::kMaxPacketsPerCapture];
    static lora_native::Stats stats;
    stats = lora_native::Stats{};
    const size_t candidate = std::min(cap_len_ / 2, trigger_offset_ + kCandidateAfterTrigger);
    lora_native::Config config{};
    config.candidate_samples = candidate;
    const size_t count = lora_native::decode_capture(cap_, cap_len_, kRate, sf_, 250000, freq_, config, packets,
                                                      lora_native::kMaxPacketsPerCapture, &stats);
    // the strongest block of the capture, as relative dBFS (the device reads its receiver's level meter; this is what the IQ itself says)
    float peak_db = -120.0f;
    for (size_t off = 0; off + 4096 <= cap_len_; off += 4096) {
      double sum = 0;
      for (size_t i = 0; i < 4096; i += 2) {
        const double re = (cap_[off + i] - 127.5) / 127.5, im = (cap_[off + i + 1] - 127.5) / 127.5;
        sum += re * re + im * im;
      }
      peak_db = std::max(peak_db, 10.0f * log10f(static_cast<float>(sum / 2048.0) + 1e-12f));
    }
    ++captures_;
    for (size_t i = 0; i < count; ++i) {
      const auto& p = packets[i];
      Json j;
      j.str("decoder", "lora").str("type", "packet").unum("seq", sequence++).unum("sender", p.sender).unum("destination", p.destination)
          .unum("packet_id", p.packet_id).unum("port", p.port).boolean("encrypted", p.encrypted).str("text", p.text)
          .str("short_name", p.short_name).str("long_name", p.long_name).num("cfo_tenths_hz", p.cfo_tenths_hz)
          .flt("level_dbfs", peak_db, 1).unum("frequency_hz", freq_).unum("sf", sf_);
      if (p.latitude_e7 != INT32_MAX && p.longitude_e7 != INT32_MAX) j.num("lat_e7", p.latitude_e7).num("lon_e7", p.longitude_e7);
      push(j.done());
      ++packets_;
      if (p.encrypted) ++encrypted_;
    }
    Json c;
    c.str("decoder", "lora").str("type", "capture").unum("seq", sequence++).unum("samples", cap_len_ / 2).unum("preambles", stats.preambles)
        .unum("header_failures", stats.header_failures).unum("crc_ok", stats.crc_ok).unum("crc_failures", stats.crc_failures)
        .unum("encrypted", stats.encrypted).unum("packets", count).flt("level_dbfs", peak_db, 1);
    push(c.done());
    last_decode_ms_ = stats.decode_millis;
    cap_len_ = 0;
    lora_preamble::reset();
  }

  // The stream is cut into fixed 16 KB blocks before anything looks at it, so the events do not depend on how the caller chunked its feeds
  // (which block a capture ends in, how much pre-roll it holds).
  int feed(const void* data, size_t bytes) override {
    if (!ok_ || bytes % 2) return -1;
    const uint8_t* iq = static_cast<const uint8_t*>(data);
    while (bytes) {
      const size_t n = std::min(bytes, kBlock - pend_);
      memcpy(pend_buf_ + pend_, iq, n);
      pend_ += n;
      iq += n;
      bytes -= n;
      if (pend_ == kBlock) {
        step(pend_buf_, kBlock);
        pend_ = 0;
      }
    }
    return 0;
  }

  void step(const uint8_t* iq, size_t bytes) {
    pre_append(iq, bytes);
    const lora_preamble::Result r = lora_preamble::feed(iq, bytes);
    if (!capturing_) {
      if (!r.fired) return;
      cap_len_ = pre_copy();
      trigger_offset_ = cap_len_ / 2;
      capturing_ = true;
      ++fires_;
      return;
    }
    const size_t take = std::min(bytes, kMaxCaptureBytes - cap_len_);
    memcpy(cap_ + cap_len_, iq, take);
    cap_len_ += take;
    if (cap_len_ >= kMaxCaptureBytes || (cap_len_ >= kMinCaptureBytes && r.quiet_samples >= kQuietSamples)) decode_capture();
  }

  int command(const char* cmd, uint64_t) override {
    if (!strcmp(cmd, "finish")) {
      if (pend_) {
        step(pend_buf_, pend_);
        pend_ = 0;
      }
      if (capturing_ && cap_len_) decode_capture();
      return 0;
    }
    if (!strcmp(cmd, "stats")) {
      const lora_preamble::Status s = lora_preamble::status();
      Json j;
      j.str("decoder", "lora").str("type", "stats").unum("seq", sequence).unum("sf", sf_).unum("windows", s.windows).unum("fires", s.fires)
          .unum("captures", captures_).unum("packets", packets_).unum("encrypted", encrypted_).flt("last_ratio", s.last_ratio, 1)
          .unum("last_bin", s.last_bin).boolean("capturing", capturing_).unum("max_window_us", s.max_window_us).unum("last_decode_ms", last_decode_ms_);
      push(j.done());
      return 0;
    }
    if (!strcmp(cmd, "slot_begin")) return 0;
    return -1;
  }

  uint8_t sf_ = 11;
  uint32_t freq_ = 0;
  uint8_t* pre_ = nullptr;
  uint8_t* cap_ = nullptr;
  size_t pre_write_ = 0, pre_fill_ = 0, cap_len_ = 0, trigger_offset_ = 0;
  bool capturing_ = false, ok_ = false;
  uint32_t captures_ = 0, packets_ = 0, encrypted_ = 0, fires_ = 0, last_decode_ms_ = 0;
  static constexpr size_t kBlock = 16384;
  uint8_t pend_buf_[kBlock];
  size_t pend_ = 0;
};

}  // namespace

orcdec* orcdec_make_lora(const char* options) {
  auto* d = new (std::nothrow) Lora(options);
  if (d && !d->ok_) {
    delete d;
    return nullptr;
  }
  return d;
}

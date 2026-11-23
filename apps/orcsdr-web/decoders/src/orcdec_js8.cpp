// JS8 (Normal submode) adapter: OrcSDR's native receive-only decoder behind the orcdec ABI.
//
// Input is 12 kS/s mono s16 (USB audio). The decoder works on whole slots; the adapter cuts the stream into slots
// by sample count (15 s for FT8, 7.5 s for FT4) so a caller can simply keep feeding audio. "slot_begin" sets the
// Unix-millisecond start of the next slot (it is reported in the events and sets DT's reference); "finish" decodes
// whatever the current slot holds.
#include "orcdec_internal.hpp"

#include "js8_native_backend.hpp"

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

namespace {

using namespace orcsdr;

struct Js8 : orcdec {
  explicit Js8(const char* options) {
    js8::native::Config config;
    const std::string k = option(options, "candidates", "");
    if (!k.empty()) config.candidate_limit = static_cast<uint16_t>(atoi(k.c_str()));
    js8::native::Memory memory;
#if defined(ESP_PLATFORM)
    memory.alloc = [](size_t bytes) -> void* {
      void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    };
    memory.release = [](void* p) { heap_caps_free(p); };
#endif
    ok_ = backend_.begin(12000, js8::Submode::normal, config, memory);
    slot_samples_ = 180000u;
    slot_ms_ = 15000u;
  }

  ~Js8() override { backend_.end(); }

  void reset() override {
    backend_.reset();
    in_slot_ = 0;
    slot_open_ = false;
    next_epoch_ms_ = 0;
  }

  void decode_slot() {
    if (!slot_open_) return;
    static ft8::Decode out[48];
    const size_t n = backend_.finish_slot(out, 48, false);
    const auto& st = backend_.stats();
    for (size_t i = 0; i < n; ++i) {
      Json j;
      j.str("decoder", "js8").str("type", "decode").unum("seq", sequence++).unum("slot_ms", epoch_ms_);
      // The SNR inside a JS8 message is the sender's report; this receiver does not measure one (flag set), so none is claimed.
      if (!(out[i].flags & ft8::decode_flag_snr_unavailable)) j.num("snr_db", out[i].snr_db);
      j.num("dt_ms", out[i].dt_ms).unum("hz", out[i].audio_hz).str("message", out[i].message);
      push(j.done());
    }
    Json s;
    s.str("decoder", "js8").str("type", "slot").unum("seq", sequence++)
        .unum("slot_ms", epoch_ms_).unum("samples", in_slot_).unum("decodes", n).boolean("too_short", st.slot_too_short);
    push(s.done());
    slot_open_ = false;
    in_slot_ = 0;
    next_epoch_ms_ = epoch_ms_ + slot_ms_;
  }

  int feed(const void* data, size_t bytes) override {
    if (!ok_ || bytes % 2) return -1;
    const int16_t* pcm = static_cast<const int16_t*>(data);   // little-endian hosts only (all our targets)
    size_t left = bytes / 2;
    while (left) {
      if (!slot_open_) {
        epoch_ms_ = next_epoch_ms_;
        if (!backend_.begin_slot(epoch_ms_)) return -1;
        slot_open_ = true;
        in_slot_ = 0;
      }
      const size_t n = std::min<size_t>(left, slot_samples_ - in_slot_);
      if (!backend_.offer_audio(pcm, n)) return -1;
      pcm += n;
      left -= n;
      in_slot_ += static_cast<uint32_t>(n);
      if (in_slot_ >= slot_samples_) decode_slot();
    }
    return 0;
  }

  int command(const char* cmd, uint64_t arg) override {
    if (!strcmp(cmd, "slot_begin")) {
      if (slot_open_ && in_slot_) decode_slot();
      next_epoch_ms_ = arg;
      return 0;
    }
    if (!strcmp(cmd, "finish")) {
      if (slot_open_ && in_slot_) decode_slot();
      return 0;
    }
    return -1;
  }

  js8::native::Backend backend_;
  bool ok_ = false;
  bool slot_open_ = false;
  uint32_t slot_samples_ = 180000, slot_ms_ = 15000, in_slot_ = 0;
  uint64_t epoch_ms_ = 0, next_epoch_ms_ = 0;
};

}  // namespace

orcdec* orcdec_make_js8(const char* options) {
  auto* d = new (std::nothrow) Js8(options);
  if (d && !d->ok_) { delete d; return nullptr; }
  return d;
}

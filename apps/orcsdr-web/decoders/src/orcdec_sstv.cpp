// SSTV adapter: OrcSDR's native receive-only SSTV decoder behind the orcdec ABI.
//
// Input is mono s16 at 48 kHz (the receiver decimates to 12 kHz itself). One image event is queued when a picture
// ends (complete, or cut short by "finish"); the pixels (RGB565) are the event's attachment. The underlying receiver
// is a singleton with static state, so only one SSTV decoder can exist at a time; a second create fails.
#include "orcdec_internal.hpp"

#include <vector>

#include "sstv_receiver.hpp"

namespace {

namespace sstv = orcsdr::sstv;

uint32_t crc32_bytes(const uint8_t* data, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

const char* ident_name(sstv::Identification i) {
  switch (i) {
    case sstv::Identification::vis: return "vis";
    case sstv::Identification::sync: return "sync";
    case sstv::Identification::manual: return "manual";
    default: return "none";
  }
}

const char* end_name(sstv::EndReason r) {
  switch (r) {
    case sstv::EndReason::complete: return "complete";
    case sstv::EndReason::sync_lost: return "sync_lost";
    case sstv::EndReason::new_vis: return "new_vis";
    case sstv::EndReason::retune: return "retune";
    case sstv::EndReason::aborted: return "aborted";
    case sstv::EndReason::captured: return "captured";
    default: return "none";
  }
}

bool g_exists = false;

struct Sstv : orcdec {
  Sstv() {
    sstv::set_mode(sstv::Mode::auto_vis);
    sstv::start(145800000);
    last_serial_ = sstv::archived_image(0).serial;
  }
  ~Sstv() override {
    sstv::stop();
    g_exists = false;
  }

  void reset() override {
    sstv::stop();
    sstv::set_mode(sstv::Mode::auto_vis);
    sstv::start(145800000);
    last_serial_ = sstv::archived_image(0).serial;
    pixels_.clear();
  }

  void collect() {
    const sstv::Image img = sstv::archived_image(0);
    if (!img.pixels || img.serial == last_serial_) return;
    last_serial_ = img.serial;
    const size_t n = static_cast<size_t>(img.width) * img.height;
    pixels_.assign(img.pixels, img.pixels + n);
    uint64_t gray = 0;
    for (uint16_t p : pixels_) gray += ((p >> 11) * 255 / 31 + ((p >> 5) & 63) * 255 / 63 + (p & 31) * 255 / 31) / 3;
    Json j;
    j.str("decoder", "sstv").str("type", "image").unum("seq", attach_seq_ = sequence++)
        .str("mode", sstv::mode_name(img.mode)).unum("vis", img.vis_code).unum("width", img.width).unum("height", img.height)
        .unum("rows", img.decoded_rows).boolean("partial", img.partial).str("identification", ident_name(img.identification))
        .str("end", end_name(img.end_reason)).unum("sync_locked", img.sync_locked).unum("sync_missed", img.sync_missed)
        .unum("luma_mean", n ? gray / n : 0)
        .unum("crc32", crc32_bytes(reinterpret_cast<const uint8_t*>(pixels_.data()), n * 2));
    push(j.done());
  }

  int feed(const void* data, size_t bytes) override {
    if (bytes % 2) return -1;
    const int16_t* pcm = static_cast<const int16_t*>(data);   // little-endian hosts only
    size_t left = bytes / 2;
    while (left) {
      const size_t n = std::min<size_t>(left, 480);   // the receiver's producer block size
      sstv::offer_audio_48k(pcm, n);
      sstv::service(16000);
      pcm += n;
      left -= n;
      collect();
    }
    return 0;
  }

  int command(const char* cmd, uint64_t) override {
    if (!strcmp(cmd, "finish")) {
      sstv::end_of_stream();
      collect();
      return 0;
    }
    if (!strcmp(cmd, "slot_begin")) return 0;
    return -1;
  }

  const void* attachment(uint32_t seq, size_t* bytes) override {
    if (seq != attach_seq_ || pixels_.empty()) return nullptr;
    *bytes = pixels_.size() * 2;
    return pixels_.data();
  }

  uint32_t last_serial_ = 0, attach_seq_ = 0xFFFFFFFFu;
  std::vector<uint16_t> pixels_;
};

}  // namespace

orcdec* orcdec_make_sstv(const char*) {
  if (g_exists) return nullptr;
  g_exists = true;
  return new (std::nothrow) Sstv();
}

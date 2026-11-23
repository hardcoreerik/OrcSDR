// ACARS adapter: OrcSDR's ACARS receive core (acars_core) behind the orcdec ABI.
//
// Input: CU8 IQ at 240 000 S/s with the AM carrier 60 kHz above the tuned centre (the front end mixes it to DC, filters and
// decimates to 24 kS/s). Option `freq=HZ` labels the messages with the channel frequency (default 0).
#include "orcdec_internal.hpp"
#include "acars_core.hpp"

#include <cstdlib>

namespace {

using namespace orcsdr::acars;

struct Acars : orcdec {
  explicit Acars(const char* options) {
    freq_ = (uint32_t)strtoul(option(options, "freq", "0").c_str(), nullptr, 10);
    decoder_.reset();
    decoder_.set_callback(&Acars::on_message, this);
    decoder_.set_frequency(freq_);
    front_.reset();
  }

  void reset() override {
    decoder_.reset();
    decoder_.set_callback(&Acars::on_message, this);
    decoder_.set_frequency(freq_);
    front_.reset();
  }

  static void on_message(void* ctx, const Message& m) {
    auto* self = static_cast<Acars*>(ctx);
    char tail[8] = {}, flight[7] = {}, label[3] = {}, one[2] = {};
    memcpy(tail, m.tail, 7);
    memcpy(flight, m.flight, 6);
    memcpy(label, m.label, 2);
    Json j;
    j.str("decoder", "acars").str("type", "message").unum("seq", self->sequence++).unum("frequency_hz", m.frequency_hz);
    one[0] = m.mode;
    j.str("mode", one);
    j.str("tail", tail);
    one[0] = m.acknowledgement;
    j.str("ack", one);
    j.str("label", label);
    one[0] = m.block_id;
    j.str("block_id", one);
    j.str("end", m.end_marker == 0x17 ? "ETB" : "ETX").boolean("corrected", m.corrected).unum("corrected_bits", m.corrected_bits)
        .unum("raw_bytes", m.raw_bytes);
    if (flight[0]) j.str("flight", flight);
    if (m.has_text) {
      char text[kMaxBlockBytes + 1] = {};
      memcpy(text, m.text, m.text_length < kMaxBlockBytes ? m.text_length : kMaxBlockBytes);
      j.str("text", text);
    }
    self->push(j.done());
  }

  static void on_sample(void* ctx, float i, float q) { static_cast<Acars*>(ctx)->decoder_.process_channel_iq(i, q); }

  int feed(const void* data, size_t bytes) override {
    if (bytes % 2) return -1;
    front_.process_cu8(static_cast<const uint8_t*>(data), bytes / 2, &Acars::on_sample, this);
    return 0;
  }

  int command(const char* cmd, uint64_t) override {
    if (!strcmp(cmd, "finish")) {
      const Stats& s = decoder_.stats();
      Json j;
      j.str("decoder", "acars").str("type", "summary").unum("seq", sequence++).unum("sync_candidates", s.sync_candidates)
          .unum("crc_ok", s.crc_ok).unum("corrected", s.corrected).unum("crc_rejected", s.crc_rejected)
          .unum("framing_rejected", s.framing_rejected).unum("overflows", s.overflows).unum("bits", s.bits);
      push(j.done());
      return 0;
    }
    if (!strcmp(cmd, "stats")) {
      const Stats& s = decoder_.stats();
      Json j;
      j.str("decoder", "acars").str("type", "stats").unum("seq", sequence)   /* a statistics snapshot is not part of the event sequence */.unum("sync_candidates", s.sync_candidates)
          .unum("crc_ok", s.crc_ok).unum("corrected", s.corrected).unum("crc_rejected", s.crc_rejected)
          .unum("framing_rejected", s.framing_rejected).unum("overflows", s.overflows).unum("bits", s.bits);
      push(j.done());
      return 0;
    }
    if (!strcmp(cmd, "slot_begin")) return 0;
    return -1;
  }

  Decoder decoder_;
  Frontend240k front_;
  uint32_t freq_ = 0;
};

}  // namespace

orcdec* orcdec_make_acars(const char* options) { return new (std::nothrow) Acars(options); }

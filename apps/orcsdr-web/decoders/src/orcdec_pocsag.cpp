// POCSAG adapter: OrcSDR's pocsag_decoder_core behind the orcdec ABI.
#include "orcdec_internal.hpp"
#include "pocsag_decoder_core.hpp"

namespace {

using namespace orcsdr::pocsag;

const char* type_name(MessageType t) {
  switch (t) {
    case MessageType::alpha: return "alpha";
    case MessageType::numeric: return "numeric";
    case MessageType::tone_only: return "tone";
    default: return "unknown";
  }
}

struct Pocsag : orcdec {
  explicit Pocsag(const char* options) {
    const std::string baud = option(options, "baud", "auto"), pol = option(options, "polarity", "auto");
    baud_ = baud == "512" ? Baud::b512 : baud == "1200" ? Baud::b1200 : baud == "2400" ? Baud::b2400 : Baud::auto_detect;
    pol_ = pol == "normal" ? Polarity::normal : pol == "inverted" ? Polarity::inverted : Polarity::auto_detect;
    decoder_.configure(baud_, pol_);
  }

  void reset() override {
    decoder_.reset();
    decoder_.configure(baud_, pol_);
  }

  static void on_message(const Message& m, void* ctx) {
    auto* self = static_cast<Pocsag*>(ctx);
    Json j;
    j.str("decoder", "pocsag").str("type", "message").unum("seq", self->sequence++)
        .unum("capcode", m.capcode).unum("function", m.function).str("kind", type_name(m.type))
        .unum("baud", m.baud).boolean("inverted", m.inverted).unum("corrected_bits", m.corrected_bits)
        .unum("uncorrectable_words", m.uncorrectable_words).boolean("truncated", m.truncated).str("text", m.text);
    self->push(j.done());
  }

  int feed(const void* data, size_t bytes) override {
    if (bytes % 2) return -1;
    decoder_.process_cu8(static_cast<const uint8_t*>(data), bytes, on_message, this);
    return 0;
  }

  int command(const char* cmd, uint64_t) override {
    if (!strcmp(cmd, "finish")) {
      const Stats& s = decoder_.stats();
      Json j;
      j.str("decoder", "pocsag").str("type", "summary").unum("seq", sequence++)
          .unum("codewords_total", s.codewords_total).unum("codewords_valid", s.codewords_valid)
          .unum("codewords_corrected", s.codewords_corrected).unum("codewords_uncorrectable", s.codewords_uncorrectable)
          .unum("batches_synced", s.batches_synced).unum("sync_losses", s.sync_losses)
          .unum("messages", s.messages_decoded).unum("detected_baud", s.detected_baud).boolean("inverted", s.inverted);
      // Outcome of the most recent codewords, oldest first: 0 valid, 1 corrected, 2 uncorrectable.
      char fec[Stats::kFecHistoryCapacity + 1];
      size_t n = s.fec_history_count < Stats::kFecHistoryCapacity ? s.fec_history_count : Stats::kFecHistoryCapacity;
      const size_t start = s.fec_history_count < Stats::kFecHistoryCapacity ? 0 : s.fec_history_write;
      for (size_t i = 0; i < n; ++i) fec[i] = static_cast<char>('0' + s.fec_history[(start + i) % Stats::kFecHistoryCapacity]);
      fec[n] = 0;
      j.str("fec", fec);
      push(j.done());
      return 0;
    }
    if (!strcmp(cmd, "stats")) {
      // Everything the pager dashboard shows: lock state, FSK measurements, symbol clusters and the FEC outcome strip.
      const Stats& s = decoder_.stats();
      Json j;
      j.str("decoder", "pocsag").str("type", "stats").unum("seq", sequence)   /* a statistics snapshot is not part of the event sequence */.unum("lock", static_cast<unsigned>(s.lock))
          .unum("detected_baud", s.detected_baud).boolean("inverted", s.inverted)
          .unum("codewords_total", s.codewords_total).unum("codewords_valid", s.codewords_valid)
          .unum("codewords_corrected", s.codewords_corrected).unum("corrected_bit_count", s.corrected_bit_count)
          .unum("codewords_uncorrectable", s.codewords_uncorrectable).unum("parity_failures", s.parity_failures)
          .unum("batches_synced", s.batches_synced).unum("sync_losses", s.sync_losses)
          .unum("messages_decoded", s.messages_decoded).unum("messages_alpha", s.messages_alpha)
          .unum("messages_numeric", s.messages_numeric).unum("messages_tone_only", s.messages_tone_only)
          .unum("messages_truncated", s.messages_truncated).flt("fsk_deviation_hz", s.fsk_deviation_hz, 1)
          .flt("frequency_offset_hz", s.frequency_offset_hz, 1);
      std::string soft = "[";
      for (size_t i = 0; i < Stats::kSoftSymbolCapacity; ++i) {
        char b[24];
        snprintf(b, sizeof b, "%s%.3f", i ? "," : "", static_cast<double>(s.soft_symbols[i]));
        soft += b;
      }
      soft += "]";
      j.raw("soft", soft).unum("soft_write", s.soft_symbol_write).unum("soft_count", s.soft_symbol_count);
      std::string fec;
      for (size_t i = 0; i < Stats::kFecHistoryCapacity; ++i) fec += static_cast<char>('0' + s.fec_history[i]);
      j.str("fec", fec.c_str()).unum("fec_write", s.fec_history_write).unum("fec_count", s.fec_history_count);
      push(j.done());
      return 0;
    }
    if (!strcmp(cmd, "slot_begin")) return 0;
    return -1;
  }

  Decoder decoder_;
  Baud baud_ = Baud::auto_detect;
  Polarity pol_ = Polarity::auto_detect;
};

}  // namespace

orcdec* orcdec_make_pocsag(const char* options) { return new (std::nothrow) Pocsag(options); }

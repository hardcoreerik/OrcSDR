// P25 Phase I adapter: OrcSDR's p25_decoder_core (C4FM / CQPSK control-channel receiver) behind the orcdec ABI.
//
// Input: CU8 IQ at 960 000 S/s centred on the channel. The voice path (IMBE vocoder) is not part of orcdec: control-channel
// traffic (identity, grants, TSBKs) is decoded, voice frames are only counted. The core keeps its state in one static
// instance, so only one P25 decoder can exist at a time.
//
// The core is fed in fixed 4096-byte slices (2048 samples), whatever the caller's chunk size, and the events are derived from
// its snapshot after each slice, so the event stream does not depend on how the input was chunked.
#include "orcdec_internal.hpp"
#include "p25_decoder_core.hpp"

#include <vector>

namespace {

using namespace orcsdr::p25core;

constexpr size_t kSlice = 4096;

struct P25 : orcdec {
  explicit P25(const char* options) {
    const std::string m = option(options, "mod", "auto");
    mod_ = m == "c4fm" ? Modulation::c4fm : m == "cqpsk" ? Modulation::cqpsk : Modulation::auto_detect;
    start();
  }

  void start() {
    configure(mod_, 0.0f, 0.0f);   // non-positive gains select the stable defaults
    orcsdr::p25core::reset(1);   // the core's reset, not this adapter's
    pending_.clear();
    samples_ = 0;
    have_identity_ = false;
    last_lock_ = false;
    last_grant_ = Grant{};
    last_grant_valid_ = false;
  }

  void reset() override { start(); }

  const char* name(Modulation m) const { return modulation_name(m); }

  void emit() {
    const Snapshot s = snapshot();
    if (s.frame_sync != last_lock_) {
      last_lock_ = s.frame_sync;
      Json j;
      j.str("decoder", "p25").str("type", "sync").unum("seq", sequence++).boolean("locked", s.frame_sync)
          .str("modulation", name(s.selected_modulation));
      push(j.done());
    }
    if (s.identity_valid && (!have_identity_ || s.nac != nac_ || s.wacn != wacn_ || s.system_id != sys_ || s.rfss != rfss_ || s.site != site_)) {
      have_identity_ = true;
      nac_ = s.nac; wacn_ = s.wacn; sys_ = s.system_id; rfss_ = s.rfss; site_ = s.site;
      Json j;
      j.str("decoder", "p25").str("type", "identity").unum("seq", sequence++).unum("nac", s.nac).unum("wacn", s.wacn)
          .unum("system_id", s.system_id).unum("rfss", s.rfss).unum("site", s.site).str("modulation", name(s.selected_modulation));
      push(j.done());
    }
    const Grant& g = s.current_grant;
    if (g.valid && (!last_grant_valid_ || g.talkgroup != last_grant_.talkgroup || g.source_id != last_grant_.source_id ||
                    g.channel_number != last_grant_.channel_number || g.frequency_hz != last_grant_.frequency_hz ||
                    g.encrypted != last_grant_.encrypted || g.emergency != last_grant_.emergency)) {
      last_grant_ = g;
      last_grant_valid_ = true;
      Json j;
      j.str("decoder", "p25").str("type", "grant").unum("seq", sequence++).unum("talkgroup", g.talkgroup).unum("source_id", g.source_id)
          .unum("channel_id", g.channel_id).unum("channel_number", g.channel_number).unum("frequency_hz", g.frequency_hz)
          .unum("slot", g.slot).boolean("tdma", g.tdma).boolean("encrypted", g.encrypted).boolean("emergency", g.emergency)
          .unum("service_options", g.service_options);
      push(j.done());
    }
  }

  void run_slice(const uint8_t* iq, size_t bytes) {
    samples_ += bytes / 2;
    const uint32_t now_ms = 1u + static_cast<uint32_t>(samples_ * 1000ull / 960000ull);
    process_cu8(iq, bytes, now_ms);
    emit();
  }

  int feed(const void* data, size_t bytes) override {
    if (bytes % 2) return -1;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    pending_.insert(pending_.end(), p, p + bytes);
    size_t used = 0;
    while (pending_.size() - used >= kSlice) {
      run_slice(pending_.data() + used, kSlice);
      used += kSlice;
    }
    pending_.erase(pending_.begin(), pending_.begin() + used);
    return 0;
  }

  int command(const char* cmd, uint64_t) override {
    if (!strcmp(cmd, "finish")) {
      if (!pending_.empty()) {
        run_slice(pending_.data(), pending_.size());
        pending_.clear();
      }
      const Snapshot s = snapshot();
      Json j;
      j.str("decoder", "p25").str("type", "summary").unum("seq", sequence++).str("modulation", name(s.selected_modulation))
          .boolean("locked", s.frame_sync).unum("sync_words", s.sync_words).unum("nid_good", s.nid_good).unum("nid_failed", s.nid_failed)
          .unum("nid_corrected_bits", s.nid_corrected_bits).unum("tsbk_good", s.tsbk_good).unum("tsbk_failed", s.tsbk_failed)
          .unum("voice_ldus", s.voice_ldus).unum("voice_frames", s.voice_frames).unum("phase2_grants", s.phase2_grants);
      push(j.done());
      return 0;
    }
    if (!strcmp(cmd, "stats")) {
      // The whole snapshot the dashboard reads; grants are flattened to cg_* (current) and rg0_* .. rg7_* (recent) keys.
      const Snapshot s = snapshot();
      Json j;
      j.str("decoder", "p25").str("type", "stats").unum("seq", sequence)   /* a statistics snapshot is not part of the event sequence */.unum("configured_modulation", (unsigned)s.configured_modulation)
          .unum("selected_modulation", (unsigned)s.selected_modulation).boolean("frame_sync", s.frame_sync).boolean("identity_valid", s.identity_valid)
          .unum("nac", s.nac).unum("wacn", s.wacn).unum("system_id", s.system_id).unum("rfss", s.rfss).unum("site", s.site)
          .unum("sync_words", s.sync_words).unum("nid_good", s.nid_good).unum("nid_failed", s.nid_failed)
          .unum("nid_corrected_bits", s.nid_corrected_bits).unum("tsbk_good", s.tsbk_good).unum("tsbk_failed", s.tsbk_failed)
          .unum("phase2_band_plans", s.phase2_band_plans).unum("phase2_grants", s.phase2_grants)
          .unum("phase2_mapping_errors", s.phase2_mapping_errors).boolean("phase2_acquisition", s.phase2_acquisition)
          .unum("voice_ldus", s.voice_ldus).unum("voice_frames", s.voice_frames).unum("last_trellis_metric", s.last_trellis_metric)
          .flt("estimated_ber_percent", s.estimated_ber_percent, 2).flt("frame_error_percent", s.frame_error_percent, 2)
          .flt("afc_offset_hz", s.afc_offset_hz, 1).flt("symbol_level", s.symbol_level, 3).flt("lock_quality_percent", s.lock_quality_percent, 1)
          .flt("timing_error", s.timing_error, 3).flt("carrier_error_hz", s.carrier_error_hz, 1).flt("decode_rate_hz", s.decode_rate_hz, 1);
      auto grant = [&](const char* prefix, const Grant& g) {
        char k[24];
        auto key = [&](const char* name) { snprintf(k, sizeof k, "%s_%s", prefix, name); return k; };
        j.boolean(key("valid"), g.valid);
        if (!g.valid) return;
        j.boolean(key("enc"), g.encrypted); j.boolean(key("emerg"), g.emergency); j.boolean(key("tdma"), g.tdma);
        j.unum(key("svc"), g.service_options); j.unum(key("chid"), g.channel_id); j.unum(key("chn"), g.channel_number);
        j.unum(key("slot"), g.slot); j.unum(key("tg"), g.talkgroup); j.unum(key("src"), g.source_id); j.unum(key("freq"), g.frequency_hz);
        j.unum(key("wacn"), g.wacn); j.unum(key("sys"), g.system_id); j.unum(key("rfss"), g.rfss); j.unum(key("site"), g.site);
        j.unum(key("seen"), g.seen_ms);
      };
      grant("cg", s.current_grant);
      for (size_t i = 0; i < kRecentGrantCount; ++i) {
        char prefix[8];
        snprintf(prefix, sizeof prefix, "rg%u", (unsigned)i);
        grant(prefix, s.recent_grants[i]);
      }
      push(j.done());
      return 0;
    }
    if (!strcmp(cmd, "slot_begin")) return 0;
    return -1;
  }

  Modulation mod_ = Modulation::auto_detect;
  std::vector<uint8_t> pending_;
  uint64_t samples_ = 0;
  bool have_identity_ = false, last_lock_ = false, last_grant_valid_ = false;
  uint16_t nac_ = 0, sys_ = 0;
  uint32_t wacn_ = 0;
  uint8_t rfss_ = 0, site_ = 0;
  Grant last_grant_{};
};

}  // namespace

orcdec* orcdec_make_p25(const char* options) { return new (std::nothrow) P25(options); }

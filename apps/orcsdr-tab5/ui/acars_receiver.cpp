#include "acars_receiver.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "acars_core.hpp"
#include "vdl2_channel.hpp"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif

namespace orcsdr::acars_receiver {
namespace {

constexpr uint32_t kSettleMs = 600;          // after a retune request: blocks from the old centre may still be in flight
constexpr uint32_t kGapResetMs = 250;        // longer without a block = an IQ discontinuity, the decoder must not bridge it
constexpr uint32_t kFlowTimeoutMs = 1500;    // no block for this long = the stream is not running
constexpr uint32_t kStatsPeriodMs = 500;
constexpr uint32_t kRetuneRetryMs = 1000;
constexpr uint32_t kStuckRetuneMs = 3000;    // wrong-rate blocks and no good block for this long: ask for our tune again

struct Rx {
  acars::Frontend240k front;
  acars::Decoder decoder;
  vdl2::Channel vdl2;               // VDL Mode 2 path, used instead of front + decoder on a 136.725-136.975 MHz channel
  float level = 0.f;
  uint32_t last_stats_ms = 0;
};

Hooks g_hooks;
Rx* g_rx = nullptr;
acars_ui::Model* g_model = nullptr;

std::atomic<bool> g_running{false};
std::atomic<bool> g_in_offer{false};      // the DSP task is inside offer_iq (start() must not reset state under it)
std::atomic<bool> g_apply_pending{false};
std::atomic<uint32_t> g_channel_hz{0};
std::atomic<uint32_t> g_center_hz{0};
std::atomic<uint32_t> g_settle_until_ms{0};
std::atomic<uint32_t> g_last_block_ms{0};
std::atomic<uint32_t> g_blocks{0};
std::atomic<uint32_t> g_wrong_rate{0};
std::atomic<uint32_t> g_settle_skipped{0};
std::atomic<uint32_t> g_discontinuities{0};
std::atomic<uint32_t> g_msgs_posted{0};
std::atomic<uint32_t> g_msgs_dropped{0};
std::atomic<uint32_t> g_max_block_us{0};
std::atomic<uint64_t> g_total_block_us{0};
std::atomic<uint32_t> g_stat_words[7];
std::atomic<uint32_t> g_level_milli{0};
std::atomic<bool> g_vdl2_mode{false};          // the active channel is VDL2 (set with the channel label once the radio accepts)
std::atomic<uint32_t> g_v2_header_ok{0};
std::atomic<uint32_t> g_v2_fec_ok{0};
std::atomic<uint32_t> g_v2_acars{0};
std::atomic<uint32_t> g_v2_raw{0};
std::atomic<uint32_t> g_v2_other{0};

// UI-task state.
uint32_t g_wanted_hz = 0;         // the channel the dashboard asked for (the decoder is only relabelled once the radio accepts it)
bool g_want_tune = false;        // a channel is wanted and the radio has not accepted it yet
bool g_owns_rate = false;        // tune() was accepted: release() must be called
bool g_reported = false;         // the model has been told the outcome of the current request
uint32_t g_tune_calls = 0;
uint32_t g_tune_refused = 0;
uint32_t g_last_tune_try_ms = 0;
uint32_t g_wrong_seen = 0;
uint32_t g_stuck_since_ms = 0;

uint32_t now_ms() { return g_hooks.now_ms ? g_hooks.now_ms() : 0u; }

#if defined(ESP_PLATFORM)
uint32_t micros32() { return static_cast<uint32_t>(esp_timer_get_time()); }
#else
uint32_t micros32() { return 0; }
#endif

void publish_stats() {
  if (!g_rx || !g_model) return;
  acars::Stats vdl2_view;
  const acars::Stats* source = &g_rx->decoder.stats();
  if (g_vdl2_mode.load(std::memory_order_relaxed)) {
    const vdl2::ReceiverStats& r = g_rx->vdl2.receiver().stats();
    const vdl2::Stats& f = g_rx->vdl2.receiver().frames_stats();
    vdl2_view.sync_candidates = r.uw_candidates;
    vdl2_view.crc_ok = f.frames_crc_ok;
    vdl2_view.crc_rejected = f.frames_crc_bad;
    vdl2_view.framing_rejected = r.header_fail + r.fec_fail;
    vdl2_view.overflows = f.overflow;
    g_v2_header_ok.store(r.header_ok, std::memory_order_relaxed);
    g_v2_fec_ok.store(r.fec_ok, std::memory_order_relaxed);
    source = &vdl2_view;
  }
  const acars::Stats& s = *source;
  g_model->post_stats(s);
  const uint32_t words[7] = {s.sync_candidates, s.crc_ok, s.corrected, s.crc_rejected, s.framing_rejected, s.overflows, s.bits};
  for (size_t i = 0; i < 7; ++i) g_stat_words[i].store(words[i], std::memory_order_relaxed);
  g_level_milli.store(static_cast<uint32_t>(g_rx->level * 1000.f), std::memory_order_relaxed);
}

void on_message(void*, const acars::Message& message) {
  if (!g_model) return;
  const uint32_t utc = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0u;
  if (g_model->post_message(message, now_ms(), utc)) g_msgs_posted.fetch_add(1, std::memory_order_relaxed);
  else g_msgs_dropped.fetch_add(1, std::memory_order_relaxed);
  publish_stats();                  // the counters move with the message, not half a second later
}

// A verified AVLC frame. ACARS inside it goes to the dashboard like an analog block. Any other data frame (ATN, CPDLC, ADS-C, ...)
// is kept as raw hex under the label "AV" with the 28-bit source address as the tail: it is shown, never interpreted. Frames that
// carry no data (supervisory acknowledgements, XID, ...) are only counted.
void on_vdl2_frame(void*, const vdl2::FrameView& frame) {
  if (!g_model) return;
  const uint32_t utc = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0u;
  if (frame.is_acars && frame.acars_message) {
    if (g_model->post_message(*frame.acars_message, now_ms(), utc)) g_msgs_posted.fetch_add(1, std::memory_order_relaxed);
    else g_msgs_dropped.fetch_add(1, std::memory_order_relaxed);
    g_v2_acars.fetch_add(1, std::memory_order_relaxed);
  } else if (frame.raw && frame.raw_size > 11 && (frame.control & 0x01u) == 0) {   // information frame (bit 0 clear) with at least one
                                                                                  // information octet after the 9 byte header and the 2 byte FCS
    static constexpr char kHex[] = "0123456789ABCDEF";
    acars::Message m;
    m.frequency_hz = g_channel_hz.load(std::memory_order_relaxed);
    m.mode = '2';
    m.label[0] = 'A';
    m.label[1] = 'V';
    for (int n = 0; n < 7; ++n) m.tail[n] = kHex[(frame.source >> (4 * (6 - n))) & 0xFu];
    m.end_marker = 0x03;
    const size_t body = frame.raw_size - 2;                       // without the frame check sequence
    const size_t bytes = body < acars::kMaxBlockBytes / 2 ? body : acars::kMaxBlockBytes / 2;
    for (size_t n = 0; n < bytes; ++n) {
      m.text[2 * n] = kHex[frame.raw[n] >> 4];
      m.text[2 * n + 1] = kHex[frame.raw[n] & 0xFu];
    }
    m.has_text = true;
    m.text_length = static_cast<uint16_t>(bytes * 2);
    m.raw_bytes = static_cast<uint16_t>(frame.raw_size);
    if (g_model->post_message(m, now_ms(), utc)) g_msgs_posted.fetch_add(1, std::memory_order_relaxed);
    else g_msgs_dropped.fetch_add(1, std::memory_order_relaxed);
    g_v2_raw.fetch_add(1, std::memory_order_relaxed);
  } else {
    g_v2_other.fetch_add(1, std::memory_order_relaxed);
  }
  publish_stats();
}

void on_sample(void* context, float i, float q) {
  Rx* rx = static_cast<Rx*>(context);
  rx->level += 0.0005f * (std::sqrt(i * i + q * q) - rx->level);
  rx->decoder.process_channel_iq(i, q);
}

void* allocate(size_t bytes) {
#if defined(ESP_PLATFORM)
  void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  return memory;
#else
  return std::malloc(bytes);
#endif
}

bool device_ready() { return g_hooks.device_ready && g_hooks.device_ready(); }

void try_tune(uint32_t now) {
  g_last_tune_try_ms = now;
  ++g_tune_calls;
  const uint32_t channel = g_wanted_hz;
  const bool vdl2_channel = is_vdl2_channel(channel);
  const uint32_t center = channel - static_cast<uint32_t>(vdl2_channel ? kVdl2CarrierOffsetHz : kCarrierOffsetHz);
  // Blocks already in flight belong to the previous centre; the DSP task drops them for a moment, then re-seeds the decoder.
  // The settle window opens before the request (the radio may retune immediately) and is withdrawn if the request is refused.
  const uint32_t previous_settle = g_settle_until_ms.load(std::memory_order_relaxed);
  g_settle_until_ms.store(now + kSettleMs, std::memory_order_release);
  const bool accepted = g_hooks.tune && g_hooks.tune(center, vdl2_channel ? kVdl2StreamRate : kStreamRate);
  if (accepted) {
    g_vdl2_mode.store(vdl2_channel, std::memory_order_relaxed);
    // Only now does the decoder get the new label: a refused request leaves the radio on the old centre, and blocks from
    // there must not be reported under the new channel.
    g_channel_hz.store(channel, std::memory_order_relaxed);
    g_center_hz.store(center, std::memory_order_relaxed);
    g_apply_pending.store(true, std::memory_order_release);
    g_owns_rate = true;
    g_want_tune = false;
    g_reported = false;
  } else {
    g_settle_until_ms.store(previous_settle, std::memory_order_release);
    ++g_tune_refused;
    g_want_tune = true;           // service() retries while the dongle is absent or busy
    if (g_model && device_ready() && !g_reported) {
      g_model->report_tuned(channel, false);
      g_reported = true;
    }
  }
}

}  // namespace

void install(const Hooks& hooks) { g_hooks = hooks; }

bool start(acars_ui::Model* model) {
  if (!model || !g_hooks.device_ready || !g_hooks.tune || !g_hooks.now_ms) return false;
  // Opening the screen again while it is open must not reset a receiver that is running (it owns the radio's sample rate).
  if (g_running.load(std::memory_order_acquire) && g_model == model) return true;
  if (!g_rx) {
    void* memory = allocate(sizeof(Rx));
    if (!memory) return false;
    g_rx = new (memory) Rx();
  }
  g_running.store(false, std::memory_order_release);
  // A block may still be inside offer_iq from a previous session (stop() only clears the flag). Wait for it, bounded.
  const uint32_t wait_from = g_hooks.now_ms();
  while (g_in_offer.load(std::memory_order_acquire)) {
    if (g_hooks.now_ms() - wait_from > 100u) return false;
  }
  if (g_owns_rate && g_hooks.release) g_hooks.release();      // never leave a stale rate override behind
  g_model = model;
  g_rx->front.reset();
  g_rx->decoder.reset();
  g_rx->decoder.set_callback(on_message, nullptr);
  g_rx->vdl2.receiver().reset();
  g_rx->vdl2.receiver().set_callback(on_vdl2_frame, nullptr);
  g_vdl2_mode.store(false, std::memory_order_relaxed);
  g_v2_header_ok.store(0);
  g_v2_fec_ok.store(0);
  g_v2_acars.store(0);
  g_v2_raw.store(0);
  g_v2_other.store(0);
  g_rx->level = 0.f;
  g_rx->last_stats_ms = 0;
  g_last_block_ms.store(0);
  g_blocks.store(0);
  g_wrong_rate.store(0);
  g_settle_skipped.store(0);
  g_discontinuities.store(0);
  g_msgs_posted.store(0);
  g_msgs_dropped.store(0);
  g_max_block_us.store(0);
  g_total_block_us.store(0);
  for (auto& word : g_stat_words) word.store(0);
  g_channel_hz.store(0);
  g_center_hz.store(0);
  g_wanted_hz = 0;
  g_settle_until_ms.store(0);
  g_apply_pending.store(false);
  g_want_tune = false;
  g_owns_rate = false;
  g_reported = false;
  g_tune_calls = g_tune_refused = 0;
  g_wrong_seen = g_stuck_since_ms = 0;
  g_v2_header_ok.store(0);
  g_v2_fec_ok.store(0);
  g_v2_acars.store(0);
  g_v2_raw.store(0);
  g_v2_other.store(0);
  acars_ui::Capabilities caps;
  caps.live_reception = true;       // this receiver decodes live signals; the other capabilities stay off
  g_model->post_capabilities(caps);
  g_model->post_receiver_state(acars_ui::ReceiverState::paused);
  g_running.store(true, std::memory_order_release);
  return true;
}

void stop() {
  if (!g_running.exchange(false, std::memory_order_acq_rel)) return;
  if (g_owns_rate && g_hooks.release) g_hooks.release();
  g_owns_rate = false;
  g_want_tune = false;
  if (g_model) {
    g_model->post_receiver_state(acars_ui::ReceiverState::disconnected);
    g_model->post_capabilities(acars_ui::Capabilities{});
    g_model->clear_tune();
  }
}

bool running() { return g_running.load(std::memory_order_acquire); }

bool request_channel(uint32_t frequency_hz) {
  if (!running() || frequency_hz < 118000000u || frequency_hz > 137000000u) return false;
  g_wanted_hz = frequency_hz;
  g_reported = false;
  try_tune(now_ms());
  return !g_want_tune;
}

void service() {
  if (!running() || !g_model) return;
  const uint32_t now = now_ms();
  const bool ready = device_ready();
  // Follow whatever channel the dashboard is asking for (preset, region change, custom frequency).
  const uint32_t requested = g_model->snapshot().requested_hz;
  if (requested != 0 && requested != g_wanted_hz && ready) {
    (void)request_channel(requested);
  } else if (g_want_tune && ready && g_wanted_hz != 0 &&
             now - g_last_tune_try_ms >= kRetuneRetryMs) {
    try_tune(now);
  } else if (g_wanted_hz == 0 && requested != 0 && ready) {
    (void)request_channel(requested);
  }

  const uint32_t last_block = g_last_block_ms.load(std::memory_order_acquire);
  const bool flowing = last_block != 0 && now - last_block < kFlowTimeoutMs &&
                       !g_apply_pending.load(std::memory_order_acquire);
  acars_ui::ReceiverState state = acars_ui::ReceiverState::paused;
  if (!ready) state = acars_ui::ReceiverState::unavailable;
  else if (flowing) state = acars_ui::ReceiverState::running;
  g_model->post_receiver_state(state);

  // Self-heal: if the radio's stream was moved to another rate behind our back (seen live on 2026-10-29: wrong-rate blocks counted
  // by the million while "running"), no good block arrives and nothing asks for the tune again. Blocks keep arriving at the wrong
  // rate for kStuckRetuneMs without a good one: ask for our tune again, then keep asking at that interval.
  const uint32_t wrong = g_wrong_rate.load(std::memory_order_relaxed);
  if (flowing) g_stuck_since_ms = 0;
  else if (wrong != g_wrong_seen && g_stuck_since_ms == 0) g_stuck_since_ms = now;
  g_wrong_seen = wrong;
  if (g_stuck_since_ms != 0 && ready && g_wanted_hz != 0 && now - g_stuck_since_ms >= kStuckRetuneMs &&
      now - g_last_tune_try_ms >= kRetuneRetryMs) {
    try_tune(now);
    g_stuck_since_ms = now;
  }

  if (flowing && !g_reported && g_hooks.tuned_center && g_hooks.tuned_center() == g_center_hz.load(std::memory_order_relaxed)) {
    g_model->report_tuned(g_channel_hz.load(std::memory_order_relaxed), true);
    g_reported = true;
  }
}

void offer_iq(const uint8_t* cu8, size_t bytes, uint32_t sample_rate) {
  if (!g_running.load(std::memory_order_acquire) || !g_rx || !g_model) return;
  g_in_offer.store(true);
  // stop() may have run between the check above and the flag: leave without touching any state.
  if (!g_running.load()) {
    g_in_offer.store(false, std::memory_order_release);
    return;
  }
  struct Leave { ~Leave() { g_in_offer.store(false, std::memory_order_release); } } leave_guard;
  const bool vdl2_mode = g_vdl2_mode.load(std::memory_order_relaxed);
  if (sample_rate != (vdl2_mode ? kVdl2StreamRate : kStreamRate)) {
    g_wrong_rate.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const uint32_t t0 = micros32();
  const uint32_t now = now_ms();
  if (now < g_settle_until_ms.load(std::memory_order_acquire)) {
    g_settle_skipped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const uint32_t last = g_last_block_ms.load(std::memory_order_relaxed);
  const bool apply = g_apply_pending.exchange(false, std::memory_order_acq_rel);
  const bool gap = !apply && last != 0 && now - last > kGapResetMs;
  if (apply || gap) {
    if (vdl2_mode) {
      // Reconfiguring restarts the channelizer and demodulator, so a retune or a gap never bridges missing samples.
      (void)g_rx->vdl2.configure(kVdl2StreamRate, 50000, kVdl2CarrierOffsetHz);
      g_rx->vdl2.receiver().set_callback(on_vdl2_frame, nullptr);
      g_rx->vdl2.receiver().set_frequency(g_channel_hz.load(std::memory_order_relaxed));
    }
    g_rx->front.reset();
    g_rx->decoder.reset_stream();
    if (apply) g_rx->decoder.set_frequency(g_channel_hz.load(std::memory_order_relaxed));
    if (gap) g_discontinuities.fetch_add(1, std::memory_order_relaxed);
  }
  g_last_block_ms.store(now, std::memory_order_release);
  g_blocks.fetch_add(1, std::memory_order_relaxed);
  if (vdl2_mode) g_rx->vdl2.process_cu8(cu8, bytes / 2);
  else g_rx->front.process_cu8(cu8, bytes / 2, on_sample, g_rx);
  if (now - g_rx->last_stats_ms >= kStatsPeriodMs) {
    g_rx->last_stats_ms = now;
    publish_stats();
  }
  const uint32_t elapsed = micros32() - t0;
  g_total_block_us.fetch_add(elapsed, std::memory_order_relaxed);
  if (elapsed > g_max_block_us.load(std::memory_order_relaxed)) g_max_block_us.store(elapsed, std::memory_order_relaxed);
}

Status status() {
  Status s;
  s.running = running();
  s.channel_hz = g_channel_hz.load(std::memory_order_relaxed);
  s.center_hz = g_center_hz.load(std::memory_order_relaxed);
  s.blocks = g_blocks.load(std::memory_order_relaxed);
  s.wrong_rate_blocks = g_wrong_rate.load(std::memory_order_relaxed);
  s.settle_skipped = g_settle_skipped.load(std::memory_order_relaxed);
  s.discontinuities = g_discontinuities.load(std::memory_order_relaxed);
  s.tune_calls = g_tune_calls;
  s.tune_refused = g_tune_refused;
  s.msgs_posted = g_msgs_posted.load(std::memory_order_relaxed);
  s.msgs_dropped = g_msgs_dropped.load(std::memory_order_relaxed);
  s.max_block_us = g_max_block_us.load(std::memory_order_relaxed);
  s.avg_block_us = s.blocks ? static_cast<uint32_t>(g_total_block_us.load(std::memory_order_relaxed) / s.blocks) : 0u;
  s.decoder.sync_candidates = g_stat_words[0].load(std::memory_order_relaxed);
  s.decoder.crc_ok = g_stat_words[1].load(std::memory_order_relaxed);
  s.decoder.corrected = g_stat_words[2].load(std::memory_order_relaxed);
  s.decoder.crc_rejected = g_stat_words[3].load(std::memory_order_relaxed);
  s.decoder.framing_rejected = g_stat_words[4].load(std::memory_order_relaxed);
  s.decoder.overflows = g_stat_words[5].load(std::memory_order_relaxed);
  s.decoder.bits = g_stat_words[6].load(std::memory_order_relaxed);
  s.level = static_cast<float>(g_level_milli.load(std::memory_order_relaxed)) / 1000.f;
  s.vdl2 = g_vdl2_mode.load(std::memory_order_relaxed);
  if (s.vdl2) {                       // the counters stay in place; they are only reported while a VDL2 channel is tuned
    s.vdl2_header_ok = g_v2_header_ok.load(std::memory_order_relaxed);
    s.vdl2_fec_ok = g_v2_fec_ok.load(std::memory_order_relaxed);
    s.vdl2_acars = g_v2_acars.load(std::memory_order_relaxed);
    s.vdl2_raw = g_v2_raw.load(std::memory_order_relaxed);
    s.vdl2_other = g_v2_other.load(std::memory_order_relaxed);
  }
  return s;
}

void format_status(char* out, size_t size) {
  if (!out || size == 0) return;
  const Status s = status();
  std::snprintf(out, size,
                "RTL_ACARS_STATUS running=%d channel_hz=%u center_hz=%u blocks=%u wrong_rate=%u settle_skipped=%u gaps=%u "
                "tune_calls=%u tune_refused=%u msgs=%u msg_drops=%u max_block_us=%u avg_block_us=%u level=%.3f sync=%u crc_ok=%u corrected=%u "
                "crc_rej=%u framing_rej=%u overflow=%u bits=%u vdl2=%d v2_hdr_ok=%u v2_fec_ok=%u v2_acars=%u v2_raw=%u v2_other=%u",
                s.running ? 1 : 0, static_cast<unsigned>(s.channel_hz), static_cast<unsigned>(s.center_hz),
                static_cast<unsigned>(s.blocks), static_cast<unsigned>(s.wrong_rate_blocks),
                static_cast<unsigned>(s.settle_skipped), static_cast<unsigned>(s.discontinuities),
                static_cast<unsigned>(s.tune_calls), static_cast<unsigned>(s.tune_refused),
                static_cast<unsigned>(s.msgs_posted), static_cast<unsigned>(s.msgs_dropped),
                static_cast<unsigned>(s.max_block_us), static_cast<unsigned>(s.avg_block_us), static_cast<double>(s.level),
                static_cast<unsigned>(s.decoder.sync_candidates), static_cast<unsigned>(s.decoder.crc_ok),
                static_cast<unsigned>(s.decoder.corrected), static_cast<unsigned>(s.decoder.crc_rejected),
                static_cast<unsigned>(s.decoder.framing_rejected), static_cast<unsigned>(s.decoder.overflows),
                static_cast<unsigned>(s.decoder.bits), s.vdl2 ? 1 : 0, static_cast<unsigned>(s.vdl2_header_ok),
                static_cast<unsigned>(s.vdl2_fec_ok), static_cast<unsigned>(s.vdl2_acars), static_cast<unsigned>(s.vdl2_raw),
                static_cast<unsigned>(s.vdl2_other));
}

}  // namespace orcsdr::acars_receiver

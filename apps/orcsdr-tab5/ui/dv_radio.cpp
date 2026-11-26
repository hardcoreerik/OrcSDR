#include "dv_radio.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "dv_channelizer.hpp"
#include "orcsdr_dmr/d1_bridge.hpp"
#include "orcsdr_dmr/d1_call.hpp"
#include "orcsdr_nxdn/decoder.hpp"
#include "orcsdr_nxdn/frontend.hpp"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace orcsdr::dv_radio {
namespace {
namespace ui = digital_voice_ui;
namespace dmr1 = dmr::d1::integration;

constexpr uint32_t kSettleMs = 600;      // after an accepted tune, blocks from the old centre may still be in flight
constexpr uint32_t kGapResetMs = 250;    // longer without a block = IQ discontinuity
constexpr uint32_t kStatsPeriodMs = 250;
constexpr size_t kSlicePairs = 2048;     // input pairs per DSP slice (bounds stack and latency)
constexpr size_t kOutPerSlice = kSlicePairs / dv::Channelizer::kDecimation + 2;
constexpr size_t kRing = 32;             // event ring
constexpr size_t kVoiceRing = 12;        // qualified voice groups waiting for the voice task (12 x 360 ms = 4.3 s)
constexpr uint32_t kSampleRing = 32768;  // 48 kS/s samples between the DSP task and the worker (0.68 s, 256 KiB PSRAM)
constexpr size_t kWorkerChunk = 512;     // samples per worker step (keeps the worker preemptible and the timing fine-grained)
static_assert((kSampleRing & (kSampleRing - 1)) == 0, "ring size must be a power of two");

// The decoders' per-sample state (filter histories and taps, ~25 KiB) is the hot working set of the worker. It lives in PSRAM.
// MEASURED on the Tab5 (2026-11-27, same firmware, same plan, 12 s of scanning each): with the state in internal RAM the DMR decoder cost
// 3.97 us per sample (worker 82 percent busy, sample ring overflowing, main loop starved to about 3 percent of core 0, touch lost);
// with the state in PSRAM it cost 1.14 us (worker 22 percent, no ring drops). The earlier belief that internal RAM is faster was wrong
// on this board, and which of the two a boot got depended on how much internal RAM happened to be free. Set kDecodersInInternalRam only
// after measuring again.
constexpr bool kDecodersInInternalRam = false;
struct Decoders {
  dmr1::Receiver dmr;
  dmr::d1::CallManager calls;   // privacy / identity / continuity gate between D1 voice groups and the voice task
  nxdn::Frontend f48{nxdn::Mode::nxdn48, 24000.0};   // fed through dec2 (half rate): NXDN48 at 48 kS/s exceeds one core
  dv::Decimator2 dec2;
  nxdn::Decoder d48{nxdn::Mode::nxdn48};
  nxdn::Frontend f96{nxdn::Mode::nxdn96};
  nxdn::Decoder d96{nxdn::Mode::nxdn96};
};
struct State {
  dv::Channelizer channelizer;
  Decoders* dec = nullptr;
  bool dec_internal = false;
  dv::Iq48 out[kOutPerSlice];                 // DSP task: channelizer output
  dmr::d1::ComplexSample dmr_in[kWorkerChunk];   // decode side (worker, or the DSP task when there is no worker)
  nxdn::IQSample nx_in[kWorkerChunk];
  Event ring[kRing];
  VoiceMsg vring[kVoiceRing];
  uint32_t last_stats_ms = 0;
};

Hooks g_hooks{};
dv_iq_rec::Recorder g_iq;                          // DEVELOPMENT IQ capture; its 5.76 MB ring is allocated only while enabled
State* g_state = nullptr;
dv::Iq48* g_ring = nullptr;                      // sample ring (PSRAM), producer = DSP task, consumer = worker
std::atomic<uint32_t> g_ring_w{0}, g_ring_r{0};  // monotonic positions
std::atomic<uint32_t> g_reset_gen{0}, g_reset_pos{0};   // DSP -> worker: discard everything before reset_pos, reset decoders
std::atomic<bool> g_worker_run{false}, g_worker_on{false};
std::atomic<uint32_t> g_dec_internal_bytes{0};
std::atomic<uint32_t> g_ring_drops{0}, g_ring_fill_max{0};
std::atomic<uint64_t> g_worker_busy_us{0};
uint32_t g_start_us_ms = 0;
std::atomic<bool> g_running{false}, g_in_offer{false}, g_apply_pending{false};
std::atomic<uint8_t> g_mask{kMaskAuto};
std::atomic<uint32_t> g_channel_hz{0}, g_epoch{0}, g_settle_until{0}, g_last_block_ms{0};
std::atomic<uint32_t> g_blocks{0}, g_wrong_rate{0}, g_settle_skipped{0}, g_gaps{0}, g_loss_events{0};
std::atomic<uint32_t> g_events{0}, g_dropped{0}, g_seq{0};
std::atomic<uint32_t> g_max_us{0}, g_level_milli{0};
std::atomic<uint64_t> g_total_us{0}, g_samples{0}, g_us_ch{0}, g_us_dmr{0}, g_us_n48{0}, g_us_n96{0};
std::atomic<uint32_t> g_verified_ms{0}, g_identity_ms{0};
std::atomic<uint32_t> g_dmr_acq{0}, g_dmr_id{0}, g_dmr_ev{0};
std::atomic<uint32_t> g_n48_frames{0}, g_n48_valid{0}, g_n96_frames{0}, g_n96_valid{0};
std::atomic<uint32_t> g_ring_head{0}, g_ring_tail{0};
std::atomic<uint32_t> g_vring_head{0}, g_vring_tail{0}, g_voice_dropped{0}, g_voice_flushes{0}, g_voice_pending_lost{0};
std::atomic<bool> g_voice_queue_on{false};
std::atomic<uint32_t> g_voice_readers{0};      // pop_voice() calls in flight: stop() waits for them before freeing the queue
std::atomic<bool> g_state_stopping{false};
uint32_t g_call_serial = 0;
// Snapshot of decoder-owned voice counters, published by the decode side (the worker) for any reader thread.
std::atomic<uint32_t> g_vs[16];
std::atomic<uint8_t> g_vpriv[3], g_vactive[3];
std::atomic<bool> g_candidate{false}, g_replay{false};
uint64_t g_last_lost = 0;
uint32_t g_tune_calls = 0, g_tune_refused = 0, g_alloc_failed = 0;
bool g_owns_rate = false;
uint32_t g_baseline_acq = 0, g_baseline_n48 = 0, g_baseline_n96 = 0;   // sync-level counters at the last tune

uint32_t now() { return g_hooks.now_ms ? g_hooks.now_ms() : 0u; }
uint32_t micros32() { return g_hooks.micros ? g_hooks.micros() : 0u; }

void* alloc_fast(size_t bytes, bool* internal) {
#if defined(ESP_PLATFORM)
  void* p = heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (p) { *internal = true; return p; }
#else
  (void)bytes;
#endif
  *internal = false;
  return nullptr;
}
void* alloc_state(size_t bytes) {
#if defined(ESP_PLATFORM)
  return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::calloc(1, bytes);
#endif
}
void free_state(void* p) {
#if defined(ESP_PLATFORM)
  heap_caps_free(p);
#else
  std::free(p);
#endif
}

void push(const Event& base) {
  if (!g_state) return;
  const uint32_t head = g_ring_head.load(std::memory_order_relaxed);
  const uint32_t tail = g_ring_tail.load(std::memory_order_acquire);
  if (head - tail >= kRing) {
    g_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  Event e = base;
  e.seq = g_seq.fetch_add(1, std::memory_order_relaxed) + 1;
  e.t_ms = now();
  e.frequency_hz = g_channel_hz.load(std::memory_order_relaxed);
  e.epoch = g_epoch.load(std::memory_order_relaxed);
  e.replay = g_replay.load(std::memory_order_relaxed);
  g_state->ring[head % kRing] = e;
  g_ring_head.store(head + 1, std::memory_order_release);
  g_events.fetch_add(1, std::memory_order_relaxed);
  g_verified_ms.store(e.t_ms ? e.t_ms : 1u, std::memory_order_release);
  if (e.evidence == ui::Evidence::identity_valid) {
    g_identity_ms.store(e.t_ms ? e.t_ms : 1u, std::memory_order_release);
    g_iq.trigger(e.t_ms, e.frequency_hz, g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0u);
  }
  g_candidate.store(true, std::memory_order_relaxed);   // a verified event is more than a candidate
}

void push_voice(const VoiceMsg& m) {
  if (!g_state || !g_voice_queue_on.load(std::memory_order_relaxed)) return;
  const uint32_t head = g_vring_head.load(std::memory_order_relaxed);
  const uint32_t tail = g_vring_tail.load(std::memory_order_acquire);
  if (head - tail >= kVoiceRing) {
    g_voice_dropped.fetch_add(1, std::memory_order_relaxed);
    if (m.kind == VoiceKind::group) g_voice_pending_lost.fetch_add(1u + m.lost_superframes, std::memory_order_relaxed);
    return;
  }
  VoiceMsg& slot = g_state->vring[head % kVoiceRing];
  slot = m;
  slot.t_ms = now();
  slot.frequency_hz = g_channel_hz.load(std::memory_order_relaxed);
  if (m.kind == VoiceKind::group) {
    const uint32_t extra = g_voice_pending_lost.exchange(0, std::memory_order_relaxed);
    slot.lost_superframes = static_cast<uint16_t>(slot.lost_superframes + extra);
  }
  g_vring_head.store(head + 1, std::memory_order_release);
}
void on_voice_group(const dmr::d1::VoiceGroup& g, void*) noexcept { g_state->dec->calls.on_group(g); }
void on_qualified(const dmr::d1::QualifiedGroup& q, void*) noexcept {
  VoiceMsg m;
  m.kind = VoiceKind::group;
  m.slot = q.slot; m.colour_code = q.colour_code; m.group_call = q.group_call; m.new_call = q.new_call;
  m.lost_superframes = static_cast<uint16_t>(q.lost_superframes > 0xffffu ? 0xffffu : q.lost_superframes);
  m.source_id = q.source_id; m.destination_id = q.destination_id;
  if (q.new_call) ++g_call_serial;
  m.call_serial = g_call_serial;
  for (size_t i = 0; i < kVoicePayloadBytes; ++i) m.payload[i] = q.group->payload[i];
  // A qualified voice group is verified activity: it extends the scanner's hold for as long as the call lasts.
  g_verified_ms.store(now() ? now() : 1u, std::memory_order_release);
  g_identity_ms.store(now() ? now() : 1u, std::memory_order_release);   // a qualified call is the strongest evidence there is
  g_iq.trigger(now(), g_channel_hz.load(std::memory_order_relaxed), g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0u);
  g_candidate.store(true, std::memory_order_relaxed);
  push_voice(m);
}
void on_voice_notice(const dmr::d1::Notice& n, void*) noexcept {
  VoiceMsg m;
  m.kind = n.kind == dmr::d1::NoticeKind::call_ended ? VoiceKind::call_ended
         : n.kind == dmr::d1::NoticeKind::protected_voice ? VoiceKind::protected_voice : VoiceKind::unqualified;
  m.slot = n.slot;
  m.call_serial = g_call_serial;
  if (n.kind != dmr::d1::NoticeKind::call_ended) g_iq.trigger(now(), g_channel_hz.load(std::memory_order_relaxed), g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0u);   // voice we could not qualify: the cases worth keeping
  push_voice(m);
}

void on_dmr(const dmr1::Event& ev, void*) noexcept {
  const dmr::d1::Event& d = ev.decoded;
  g_state->dec->calls.on_data(d);
  g_dmr_ev.fetch_add(1, std::memory_order_relaxed);
  // Only events that passed RS/CRC (integrity_valid) become UI events; sync/slot-type alone stay counters.
  if (!d.integrity_valid) return;
  Event e;
  e.protocol = ui::Protocol::dmr;
  const bool identity = d.evidence == dmr::d1::Evidence::identity_valid && d.identity != dmr::d1::IdentityKind::none;
  e.evidence = identity ? ui::Evidence::identity_valid : ui::Evidence::integrity_valid;
  if (identity) {
    e.source_id = d.source_id;
    e.destination_id = d.destination_id;
    e.has_source = e.has_destination = true;
    e.group_call = d.identity == dmr::d1::IdentityKind::group;
    g_dmr_id.fetch_add(1, std::memory_order_relaxed);
  }
  e.colour_code = d.colour_code;
  e.colour_code_valid = d.golay_valid || d.late_entry;   // CC comes from the Golay-checked slot type, or from the QR(16,7,6)-checked EMB of a checksummed late-entry LC
  e.slot = d.slot;
  e.slot_valid = d.slot != 0;                 // 0 stays UNKNOWN; never defaulted to TS1
  e.has_data_type = d.golay_valid && d.data_type != 0xff; e.data_type = d.data_type; e.late_entry = d.late_entry;
  e.flco = d.flco; e.feature_id = d.feature_id; e.service_options = d.service_options;
  if (d.info_valid) { e.info_valid = true; for (size_t i = 0; i < 12; ++i) e.info[i] = d.info[i]; }
  push(e);
}

void on_nxdn_event(const nxdn::Event& ev, void*) noexcept {
  Event e;
  const bool is96 = ev.mode == nxdn::Mode::nxdn96;
  e.protocol = is96 ? ui::Protocol::nxdn96 : ui::Protocol::nxdn48;
  e.evidence = ui::Evidence::integrity_valid;   // LICH parity + SACCH FEC/CRC; no identity is inferred
  e.ran = ev.ran;
  e.ran_valid = true;                            // RAN 0 is a valid value
  e.nxdn_rf_channel = ev.rf_channel;
  e.nxdn_rf_valid = true;
  (is96 ? g_n96_valid : g_n48_valid).fetch_add(1, std::memory_order_relaxed);
  push(e);
}
void on_nxdn_frame_48(const nxdn::FrameObservation&, void*) noexcept { g_n48_frames.fetch_add(1, std::memory_order_relaxed); }
void on_nxdn_frame_96(const nxdn::FrameObservation&, void*) noexcept { g_n96_frames.fetch_add(1, std::memory_order_relaxed); }
void on_dibit_48(std::uint8_t d, void*) noexcept {
  (void)g_state->dec->d48.feed(&d, 1, on_nxdn_event, nullptr, on_nxdn_frame_48, nullptr);
}
void on_dibit_96(std::uint8_t d, void*) noexcept {
  (void)g_state->dec->d96.feed(&d, 1, on_nxdn_event, nullptr, on_nxdn_frame_96, nullptr);
}

void reset_channelizer() { g_state->channelizer.reset(); }
void reset_decoders() {
  g_state->dec->dmr.reset();
  g_state->dec->calls.reset();
  g_state->dec->dmr.set_voice_sink(on_voice_group, nullptr);
  g_state->dec->calls.set_sinks(on_qualified, on_voice_notice, nullptr);
  {   // the voice task must drop whatever speech it is holding: nothing may bridge a stream discontinuity
    VoiceMsg flush;
    flush.kind = VoiceKind::flush;
    push_voice(flush);
    g_voice_flushes.fetch_add(1, std::memory_order_relaxed);
  }
  g_state->dec->f48.reset();
  g_state->dec->dec2.reset();
  g_state->dec->d48.reset();
  g_state->dec->f96.reset();
  g_state->dec->d96.reset();
}

void publish_stats() {
  {
    static uint64_t prev_anchors = 0;                      // a voice SYNC that never became a complete superframe also triggers
    const uint64_t a = g_state->dec->dmr.voice_stats().anchors;
    if (a < prev_anchors) prev_anchors = a;
    if (a > prev_anchors) g_iq.trigger(now(), g_channel_hz.load(std::memory_order_relaxed), g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0u);
    prev_anchors = a;
    g_iq.service(now());
  }
  {
    const auto& c = g_state->dec->calls.stats();
    const auto& v = g_state->dec->dmr.voice_stats();
    const uint32_t vals[16] = {static_cast<uint32_t>(v.anchors), static_cast<uint32_t>(c.groups_seen),
        static_cast<uint32_t>(c.groups_qualified), static_cast<uint32_t>(c.groups_protected), static_cast<uint32_t>(c.groups_unqualified),
        static_cast<uint32_t>(c.calls_started), static_cast<uint32_t>(c.calls_ended), static_cast<uint32_t>(c.lost_superframes),
        static_cast<uint32_t>(v.abandoned_emb), static_cast<uint32_t>(v.abandoned_sync), static_cast<uint32_t>(v.embedded_lc_ok),
        static_cast<uint32_t>(v.embedded_lc_bad), 0, 0, 0, 0};
    for (int i = 0; i < 16; ++i) g_vs[i].store(vals[i], std::memory_order_relaxed);
    for (unsigned sl = 1; sl <= 2; ++sl) {
      g_vpriv[sl].store(static_cast<uint8_t>(g_state->dec->calls.privacy(sl)), std::memory_order_relaxed);
      g_vactive[sl].store(g_state->dec->calls.active(sl) ? 1 : 0, std::memory_order_relaxed);
    }
  }
  const auto& s = g_state->dec->dmr.stats();
  g_dmr_acq.store(static_cast<uint32_t>(s.acquisitions), std::memory_order_relaxed);
  const uint32_t acq = static_cast<uint32_t>(s.acquisitions);
  const uint32_t n48 = g_n48_frames.load(std::memory_order_relaxed);
  const uint32_t n96 = g_n96_frames.load(std::memory_order_relaxed);
  if (acq > g_baseline_acq || n48 > g_baseline_n48 || n96 > g_baseline_n96) g_candidate.store(true, std::memory_order_relaxed);
}

// Decode side. Runs on the worker, or inline in offer_iq when there is no worker. Single consumer of the sample ring.
void decode_samples(const dv::Iq48* in, size_t n, uint8_t mask) {
  State& st = *g_state;
  uint32_t a = micros32();
  if (mask & kMaskDmr) {
    for (size_t i = 0; i < n; ++i) st.dmr_in[i] = {in[i].re, in[i].im};
    (void)st.dec->dmr.process(st.dmr_in, n, on_dmr, nullptr);
    st.dec->calls.on_time(st.dec->dmr.protocol_dibits());
  }
  uint32_t b = micros32();
  g_us_dmr.fetch_add(b - a, std::memory_order_relaxed);
  if (mask & (kMaskNxdn48 | kMaskNxdn96)) {
    for (size_t i = 0; i < n; ++i) st.nx_in[i] = {in[i].re, in[i].im};
    a = micros32();
    if (mask & kMaskNxdn48) {
      dv::Iq48 half[kWorkerChunk / 2 + 2];
      const size_t m = st.dec->dec2.process(in, n, half, sizeof(half) / sizeof(half[0]));
      nxdn::IQSample half_nx[kWorkerChunk / 2 + 2];
      for (size_t i = 0; i < m; ++i) half_nx[i] = {half[i].re, half[i].im};
      (void)st.dec->f48.feed(half_nx, m, on_dibit_48, nullptr);
    }
    b = micros32();
    g_us_n48.fetch_add(b - a, std::memory_order_relaxed);
    if (mask & kMaskNxdn96) (void)st.dec->f96.feed(st.nx_in, n, on_dibit_96, nullptr);
    g_us_n96.fetch_add(micros32() - b, std::memory_order_relaxed);
  }
}

// Worker: drains the ring in small chunks. A reset request from the DSP task discards every sample queued before it.
void worker_main(void*) {
  uint32_t seen_gen = g_reset_gen.load(std::memory_order_acquire);
  uint32_t last_stats = 0;
  while (g_worker_run.load(std::memory_order_acquire)) {
    const uint32_t gen = g_reset_gen.load(std::memory_order_acquire);
    if (gen != seen_gen) {
      seen_gen = gen;
      const uint32_t pos = g_reset_pos.load(std::memory_order_acquire);
      if (static_cast<int32_t>(pos - g_ring_r.load(std::memory_order_relaxed)) > 0) g_ring_r.store(pos, std::memory_order_release);
      reset_decoders();
    }
    const uint32_t r = g_ring_r.load(std::memory_order_relaxed);
    const uint32_t w = g_ring_w.load(std::memory_order_acquire);
    uint32_t avail = w - r;
    if (avail == 0) {
      if (g_hooks.sleep_ms) g_hooks.sleep_ms(2);
      continue;
    }
    if (avail > kWorkerChunk) avail = kWorkerChunk;
    dv::Iq48 local[kWorkerChunk];
    for (uint32_t i = 0; i < avail; ++i) local[i] = g_ring[(r + i) & (kSampleRing - 1)];
    const uint32_t t0 = micros32();
    decode_samples(local, avail, g_mask.load(std::memory_order_relaxed));
    g_worker_busy_us.fetch_add(micros32() - t0, std::memory_order_relaxed);
    // A reset requested while this chunk decoded makes it stale: do not advance past the reset position.
    if (g_reset_gen.load(std::memory_order_acquire) == seen_gen) g_ring_r.store(r + avail, std::memory_order_release);
    const uint32_t t = now();
    if (t - last_stats >= kStatsPeriodMs) {
      last_stats = t;
      publish_stats();
    }
  }
}
}  // namespace

const char* link_name(Link link) {
  switch (link) {
    case Link::stopped: return "stopped";
    case Link::no_memory: return "no_memory";
    case Link::no_receiver: return "no_receiver";
    case Link::tuning: return "tuning";
    case Link::receiving: return "receiving";
    case Link::wrong_stream: return "wrong_stream";
    case Link::stalled: return "stalled";
  }
  return "?";
}

void install(const Hooks& hooks) { g_hooks = hooks; }

bool start() {
  if (g_running.load()) return true;
  if (!g_hooks.tune || !g_hooks.now_ms || !g_hooks.device_ready) return false;
  if (!g_state) {
    void* mem = alloc_state(sizeof(State));
    if (!mem) {
      g_alloc_failed = static_cast<uint32_t>(sizeof(State));
      return false;
    }
    g_state = new (mem) State();
    bool internal = false;
    void* dm = kDecodersInInternalRam ? alloc_fast(sizeof(Decoders), &internal) : nullptr;
    if (!dm) dm = alloc_state(sizeof(Decoders));
    if (!dm) {
      g_alloc_failed = static_cast<uint32_t>(sizeof(Decoders));
      g_state->~State();
      free_state(g_state);
      g_state = nullptr;
      return false;
    }
    g_state->dec = new (dm) Decoders();
    g_state->dec_internal = internal;
    g_dec_internal_bytes.store(internal ? static_cast<uint32_t>(sizeof(Decoders)) : 0u);
  }
  const bool want_worker = g_hooks.spawn_worker != nullptr;
  if (want_worker && !g_ring) {
    g_ring = static_cast<dv::Iq48*>(alloc_state(sizeof(dv::Iq48) * kSampleRing));
    if (!g_ring) {
      g_alloc_failed = static_cast<uint32_t>(sizeof(dv::Iq48) * kSampleRing);
      g_state->~State();
      free_state(g_state);
      g_state = nullptr;
      return false;
    }
  }
  reset_channelizer();
  reset_decoders();
  g_ring_w = g_ring_r = 0; g_reset_gen = g_reset_pos = 0; g_ring_drops = g_ring_fill_max = 0; g_worker_busy_us = 0;
  g_ring_head.store(0); g_ring_tail.store(0);
  g_vring_head.store(0); g_vring_tail.store(0); g_voice_dropped = g_voice_flushes = g_voice_pending_lost = 0; g_call_serial = 0;
  g_blocks = g_wrong_rate = g_settle_skipped = g_gaps = g_loss_events = 0;
  g_events = g_dropped = g_seq = 0;
  g_max_us = g_level_milli = 0; g_total_us = 0; g_samples = 0; g_verified_ms = 0; g_identity_ms = 0;
  g_us_ch = g_us_dmr = g_us_n48 = g_us_n96 = 0;
  g_dmr_acq = g_dmr_id = g_dmr_ev = 0;
  g_n48_frames = g_n48_valid = g_n96_frames = g_n96_valid = 0;
  g_replay = false; g_candidate = false; g_channel_hz = 0; g_epoch = 0; g_settle_until = 0; g_last_block_ms = 0;
  g_baseline_acq = g_baseline_n48 = g_baseline_n96 = 0;
  g_last_lost = g_hooks.lost_samples ? g_hooks.lost_samples() : 0;
  g_tune_calls = g_tune_refused = 0;
  g_apply_pending = false;
  g_start_us_ms = now();
  g_worker_on = false;
  if (want_worker) {
    g_worker_run.store(true, std::memory_order_release);
    if (g_hooks.spawn_worker(worker_main, nullptr)) {
      g_worker_on.store(true, std::memory_order_release);
    } else {
      g_worker_run.store(false, std::memory_order_release);   // no worker: decode inline (slower, never silent)
    }
  }
  g_running.store(true, std::memory_order_release);
  return true;
}

void stop() {
  g_running.store(false, std::memory_order_release);
  if (g_worker_on.load()) {
    g_worker_run.store(false, std::memory_order_release);
    if (g_hooks.join_worker) g_hooks.join_worker();
    g_worker_on.store(false, std::memory_order_release);
  }
  if (!g_state) return;
  g_state_stopping.store(true, std::memory_order_release);
  for (uint32_t waited = now(); g_voice_readers.load(std::memory_order_acquire) != 0 && now() - waited < 100u;) {}   // bounded
  const uint32_t from = now();
  while (g_in_offer.load(std::memory_order_acquire)) {   // bounded wait for a block that is mid-flight
    if (now() - from > 100u) break;
  }
  if (g_owns_rate && g_hooks.release) g_hooks.release();
  g_owns_rate = false;
  if (g_ring) { free_state(g_ring); g_ring = nullptr; }
  if (g_state->dec) { g_state->dec->~Decoders(); free_state(g_state->dec); g_state->dec = nullptr; }
  g_state->~State();
  free_state(g_state);
  g_state = nullptr;
  g_state_stopping.store(false, std::memory_order_release);
  g_channel_hz = 0;
}

bool running() { return g_running.load(std::memory_order_acquire); }
void request_decoder_reset() {
  if (!g_state) return;
  if (g_worker_on.load(std::memory_order_relaxed)) {
    g_reset_pos.store(g_ring_w.load(std::memory_order_relaxed), std::memory_order_release);
    g_reset_gen.fetch_add(1, std::memory_order_acq_rel);
  } else {
    reset_decoders();
  }
}
void set_protocol_mask(uint8_t mask) { g_mask.store(mask & kMaskAuto, std::memory_order_relaxed); }

bool tune_channel(uint32_t channel_hz) {
  if (!g_running.load() || !g_state || !g_hooks.tune || channel_hz <= static_cast<uint32_t>(kChannelOffsetHz)) return false;
  ++g_tune_calls;
  const bool ok = g_hooks.tune(channel_hz - static_cast<uint32_t>(kChannelOffsetHz), kStreamRate);
  if (!ok) {
    ++g_tune_refused;
    return false;
  }
  g_owns_rate = true;
  g_channel_hz.store(channel_hz, std::memory_order_relaxed);
  g_settle_until.store(now() + kSettleMs, std::memory_order_release);
  g_verified_ms.store(0, std::memory_order_release);
  g_identity_ms.store(0, std::memory_order_release);
  g_candidate.store(false, std::memory_order_relaxed);
  g_baseline_acq = g_dmr_acq.load(); g_baseline_n48 = g_n48_frames.load(); g_baseline_n96 = g_n96_frames.load();
  g_epoch.fetch_add(1, std::memory_order_relaxed);
  g_apply_pending.store(true, std::memory_order_release);   // the DSP task resets every decoder before the next block
  return true;
}

void service() {}

bool pop_voice(VoiceMsg* out) {
  // The voice task may call this while the UI task stops the receiver and frees the state: register as a reader first.
  g_voice_readers.fetch_add(1, std::memory_order_acq_rel);
  bool got = false;
  if (g_state && out && !g_state_stopping.load(std::memory_order_acquire)) {
    const uint32_t tail = g_vring_tail.load(std::memory_order_relaxed);
    if (tail != g_vring_head.load(std::memory_order_acquire)) {
      *out = g_state->vring[tail % kVoiceRing];
      g_vring_tail.store(tail + 1, std::memory_order_release);
      got = true;
    }
  }
  g_voice_readers.fetch_sub(1, std::memory_order_acq_rel);
  return got;
}
void iq_set_enabled(bool on) { g_iq.set_enabled(on); }
bool iq_enabled() { return g_iq.enabled(); }
bool iq_ready(const uint8_t** data, size_t* bytes, char* name, size_t name_size) { g_iq.service(now()); return g_iq.ready(data, bytes, name, name_size); }
void iq_saving() { g_iq.saving(); }
void iq_snap() { g_iq.trigger(now(), g_channel_hz.load(std::memory_order_relaxed), g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0u); }
void iq_release(bool saved) { g_iq.release(saved, now()); }
dv_iq_rec::Stats iq_stats() { return g_iq.stats(); }
void set_voice_queue_enabled(bool enabled) { g_voice_queue_on.store(enabled, std::memory_order_release); }
VoiceStatus voice_status() {
  VoiceStatus v;
  v.anchors = g_vs[0].load(std::memory_order_relaxed); v.groups_seen = g_vs[1].load(std::memory_order_relaxed);
  v.qualified = g_vs[2].load(std::memory_order_relaxed); v.protected_groups = g_vs[3].load(std::memory_order_relaxed);
  v.unqualified = g_vs[4].load(std::memory_order_relaxed); v.calls_started = g_vs[5].load(std::memory_order_relaxed);
  v.calls_ended = g_vs[6].load(std::memory_order_relaxed); v.lost_superframes = g_vs[7].load(std::memory_order_relaxed);
  v.emb_abandoned = g_vs[8].load(std::memory_order_relaxed); v.sync_abandoned = g_vs[9].load(std::memory_order_relaxed);
  v.embedded_lc_ok = g_vs[10].load(std::memory_order_relaxed); v.embedded_lc_bad = g_vs[11].load(std::memory_order_relaxed);
  v.queue_drops = g_voice_dropped.load(std::memory_order_relaxed); v.flushes = g_voice_flushes.load(std::memory_order_relaxed);
  v.queue_pending = g_vring_head.load(std::memory_order_acquire) - g_vring_tail.load(std::memory_order_relaxed);
  for (unsigned i = 1; i <= 2; ++i) { v.privacy[i] = g_vpriv[i].load(std::memory_order_relaxed); v.active[i] = g_vactive[i].load(std::memory_order_relaxed) != 0; }
  return v;
}

bool pop_event(Event* out) {
  if (!g_state || !out) return false;
  const uint32_t tail = g_ring_tail.load(std::memory_order_relaxed);
  if (tail == g_ring_head.load(std::memory_order_acquire)) return false;
  *out = g_state->ring[tail % kRing];
  g_ring_tail.store(tail + 1, std::memory_order_release);
  return true;
}

namespace {
void offer_internal(const uint8_t* cu8, size_t bytes, uint32_t sample_rate, bool from_replay) {
  if (!g_running.load(std::memory_order_acquire) || !g_state) return;
  if (g_replay.load(std::memory_order_acquire) != from_replay) return;   // live blocks are ignored during a replay, and vice versa
  g_in_offer.store(true);
  if (!g_running.load()) {
    g_in_offer.store(false, std::memory_order_release);
    return;
  }
  struct Leave { ~Leave() { g_in_offer.store(false, std::memory_order_release); } } leave_guard;
  if (sample_rate != kStreamRate) {
    g_wrong_rate.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const uint32_t t0 = micros32();
  const uint32_t t = now();
  if (g_channel_hz.load(std::memory_order_relaxed) == 0) return;        // no channel asked for yet
  if (static_cast<int32_t>(t - g_settle_until.load(std::memory_order_acquire)) < 0) {
    g_settle_skipped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const uint32_t last = g_last_block_ms.load(std::memory_order_relaxed);
  bool reset = g_apply_pending.exchange(false, std::memory_order_acq_rel);
  const bool gap = !reset && last != 0 && t - last > kGapResetMs;
  if (g_hooks.lost_samples && !from_replay) {
    const uint64_t lost = g_hooks.lost_samples();
    if (lost != g_last_lost) {
      g_last_lost = lost;
      g_loss_events.fetch_add(1, std::memory_order_relaxed);
      reset = true;
    }
  }
  if (gap) g_gaps.fetch_add(1, std::memory_order_relaxed);
  const bool worker = g_worker_on.load(std::memory_order_relaxed);
  auto request_decoder_reset = [&]() {
    if (worker) {
      g_reset_pos.store(g_ring_w.load(std::memory_order_relaxed), std::memory_order_release);   // drop everything queued
      g_reset_gen.fetch_add(1, std::memory_order_acq_rel);
    } else {
      reset_decoders();
    }
  };
  if (reset || gap) {
    reset_channelizer();
    request_decoder_reset();
    if (!g_apply_pending.load()) g_epoch.fetch_add(1, std::memory_order_relaxed);
  }
  if (reset || gap) g_iq.reset();
  g_last_block_ms.store(t, std::memory_order_release);
  if (!from_replay) g_iq.feed(cu8, bytes);
  g_blocks.fetch_add(1, std::memory_order_relaxed);
  const size_t pairs_total = bytes / 2;
  g_samples.fetch_add(pairs_total, std::memory_order_relaxed);
  const uint8_t mask = g_mask.load(std::memory_order_relaxed);
  State& st = *g_state;
  for (size_t off = 0; off < pairs_total; off += kSlicePairs) {
    const size_t pairs = pairs_total - off < kSlicePairs ? pairs_total - off : kSlicePairs;
    const uint32_t a = micros32();
    const size_t n = st.channelizer.process_cu8(cu8 + 2 * off, pairs, st.out, kOutPerSlice);
    g_us_ch.fetch_add(micros32() - a, std::memory_order_relaxed);
    if (worker) {
      const uint32_t w = g_ring_w.load(std::memory_order_relaxed);
      const uint32_t used = w - g_ring_r.load(std::memory_order_acquire);
      if (used + n > kSampleRing) {                 // the worker fell behind: drop, never wait, never decode stale data
        g_ring_drops.fetch_add(1, std::memory_order_relaxed);
        request_decoder_reset();
        g_epoch.fetch_add(1, std::memory_order_relaxed);
      } else {
        for (size_t i = 0; i < n; ++i) g_ring[(w + i) & (kSampleRing - 1)] = st.out[i];
        g_ring_w.store(w + static_cast<uint32_t>(n), std::memory_order_release);
        if (used + n > g_ring_fill_max.load(std::memory_order_relaxed))
          g_ring_fill_max.store(used + static_cast<uint32_t>(n), std::memory_order_relaxed);
      }
    } else {
      for (size_t i = 0; i < n; i += kWorkerChunk)
        decode_samples(st.out + i, n - i < kWorkerChunk ? n - i : kWorkerChunk, mask);
    }
  }
  g_level_milli.store(static_cast<uint32_t>(st.channelizer.take_level() * 1000.0f), std::memory_order_relaxed);
  if (!worker && t - st.last_stats_ms >= kStatsPeriodMs) {
    st.last_stats_ms = t;
    publish_stats();
  }
  const uint32_t us = micros32() - t0;
  g_total_us.fetch_add(us, std::memory_order_relaxed);
  if (us > g_max_us.load(std::memory_order_relaxed)) g_max_us.store(us, std::memory_order_relaxed);
}

}  // namespace

void offer_iq(const uint8_t* cu8, size_t bytes, uint32_t sample_rate) { offer_internal(cu8, bytes, sample_rate, false); }
void feed_replay(const uint8_t* cu8, size_t bytes) { offer_internal(cu8, bytes, kStreamRate, true); }

bool begin_replay(uint32_t channel_hz) {
  if (!g_running.load() || !g_state || channel_hz == 0) return false;
  g_replay.store(true, std::memory_order_release);
  g_channel_hz.store(channel_hz, std::memory_order_relaxed);
  g_settle_until.store(now(), std::memory_order_release);           // a file has no settle time
  g_verified_ms.store(0, std::memory_order_release);
  g_candidate.store(false, std::memory_order_relaxed);
  g_epoch.fetch_add(1, std::memory_order_relaxed);
  g_apply_pending.store(true, std::memory_order_release);
  return true;
}

Status status() {
  Status s;
  s.running = g_running.load();
  s.replay = g_replay.load();
  s.mask = g_mask.load();
  s.channel_hz = g_channel_hz.load();
  s.epoch = g_epoch.load();
  s.blocks = g_blocks.load();
  s.wrong_rate_blocks = g_wrong_rate.load();
  s.settle_skipped = g_settle_skipped.load();
  s.discontinuities = g_gaps.load();
  s.loss_events = g_loss_events.load();
  s.tune_calls = g_tune_calls;
  s.tune_refused = g_tune_refused;
  s.events = g_events.load();
  s.events_dropped = g_dropped.load();
  s.dmr_acquisitions = g_dmr_acq.load();
  s.dmr_identities = g_dmr_id.load();
  s.dmr_events = g_dmr_ev.load();
  s.nxdn48_frames = g_n48_frames.load();
  s.nxdn48_sacch_valid = g_n48_valid.load();
  s.nxdn96_frames = g_n96_frames.load();
  s.nxdn96_sacch_valid = g_n96_valid.load();
  s.max_block_us = g_max_us.load();
  s.avg_block_us = s.blocks ? static_cast<uint32_t>(g_total_us.load() / s.blocks) : 0;
  s.level_milli = g_level_milli.load();
  s.worker = g_worker_on.load();
  { const uint32_t w = g_ring_w.load(), r = g_ring_r.load(); s.ring_pending = w - r; }
  s.ring_fill_max = g_ring_fill_max.load();
  s.ring_drops = g_ring_drops.load();
  { const uint32_t el = now() - g_start_us_ms;
    s.worker_busy_permille = el ? static_cast<uint32_t>(g_worker_busy_us.load() / 1000ull * 1000ull / el) : 0; }
  s.samples_in = g_samples.load();
  s.state_bytes = g_state ? static_cast<uint32_t>(sizeof(State) + sizeof(Decoders)) : 0;
  s.alloc_failed_bytes = g_alloc_failed;
  s.verified_ms = g_verified_ms.load();
  s.identity_ms = g_identity_ms.load();
  s.candidate = g_candidate.load();
  if (!s.running) s.link = g_alloc_failed && !g_state ? Link::no_memory : Link::stopped;
  else if (!g_hooks.device_ready || !g_hooks.device_ready()) s.link = Link::no_receiver;
  else if (s.channel_hz == 0 || (s.blocks == 0 && s.wrong_rate_blocks == 0)) s.link = Link::tuning;
  else if (s.blocks == 0) s.link = Link::wrong_stream;
  else if (now() - g_last_block_ms.load() > 1500u) s.link = Link::stalled;
  else s.link = Link::receiving;
  return s;
}

void format_status(char* out, size_t size) {
  if (!out || size == 0) return;
  const Status s = status();
  std::snprintf(out, size,
      "RTL_DV_STATUS link=%s running=%d ch_hz=%lu epoch=%lu blocks=%lu wrong_rate=%lu settle_skipped=%lu gaps=%lu loss=%lu "
      "tune=%lu/%lu events=%lu dropped=%lu dmr_acq=%lu dmr_ev=%lu dmr_id=%lu n48_frames=%lu n48_ok=%lu n96_frames=%lu n96_ok=%lu "
      "blk_us_max=%lu blk_us_avg=%lu level_milli=%lu mask=%u state_bytes=%lu pairs=%llu us_ch=%llu us_dmr=%llu us_n48=%llu us_n96=%llu worker=%d ring_pend=%lu ring_max=%lu ring_drops=%lu worker_busy_pm=%lu dec_internal=%lu",
      link_name(s.link), s.running ? 1 : 0, (unsigned long)s.channel_hz, (unsigned long)s.epoch, (unsigned long)s.blocks,
      (unsigned long)s.wrong_rate_blocks, (unsigned long)s.settle_skipped, (unsigned long)s.discontinuities,
      (unsigned long)s.loss_events, (unsigned long)s.tune_calls, (unsigned long)s.tune_refused, (unsigned long)s.events,
      (unsigned long)s.events_dropped, (unsigned long)s.dmr_acquisitions, (unsigned long)s.dmr_events,
      (unsigned long)s.dmr_identities, (unsigned long)s.nxdn48_frames, (unsigned long)s.nxdn48_sacch_valid,
      (unsigned long)s.nxdn96_frames, (unsigned long)s.nxdn96_sacch_valid, (unsigned long)s.max_block_us,
      (unsigned long)s.avg_block_us, (unsigned long)s.level_milli, (unsigned)s.mask, (unsigned long)s.state_bytes,
      (unsigned long long)s.samples_in, (unsigned long long)g_us_ch.load(), (unsigned long long)g_us_dmr.load(),
      (unsigned long long)g_us_n48.load(), (unsigned long long)g_us_n96.load(), s.worker ? 1 : 0,
      (unsigned long)s.ring_pending, (unsigned long)s.ring_fill_max, (unsigned long)s.ring_drops,
      (unsigned long)s.worker_busy_permille, (unsigned long)g_dec_internal_bytes.load());
}

}  // namespace orcsdr::dv_radio

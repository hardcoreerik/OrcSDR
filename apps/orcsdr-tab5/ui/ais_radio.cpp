#include "ais_radio.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "ais_snapshot_adapter.hpp"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace orcsdr::ais_radio {
namespace {

constexpr uint64_t kSettleMs = 600;          // blocks in flight after a tune belong to the previous centre
constexpr uint64_t kPublishMs = 500;
constexpr uint64_t kFlowTimeoutMs = 1500;    // no decoded block for this long = not receiving
constexpr uint64_t kStuckRetuneMs = 3000;    // refused blocks keep arriving for this long: ask for the tune again
constexpr uint64_t kRetuneRetryMs = 3000;
constexpr uint64_t kPositionStaleMs = 6ull * 60ull * 1000ull;   // twice the slowest routine Class A/B position interval
constexpr uint64_t kPruneIdleMs = 20ull * 60ull * 1000ull;      // a station silent this long leaves the store

// Everything large lives here, allocated on start() and freed on stop(). Nothing below is a static of meaningful size.
struct State {
  ais_rx::Pipeline pipeline;                 // DSP task only
  ais_ui::Snapshot work;                     // DSP task only: targets and journal being assembled
  uint64_t message_rx_ms[ais_ui::kMessages]{};
  ais_ui::Snapshot shared;                   // guarded by g_feed_lock
  ais_rx::PipelineStats shared_stats;        // guarded by g_feed_lock
  uint64_t last_publish_ms = 0;
  uint64_t started_ms = 0;
  uint64_t lost_baseline = 0, lost_seen = 0;
  uint32_t offered_epoch = 0;
  bool lost_primed = false;
};

Hooks g_hooks;
State* g_state = nullptr;
std::atomic<bool> g_running{false};
std::atomic<bool> g_in_offer{false};
std::atomic_flag g_feed_lock = ATOMIC_FLAG_INIT;
std::atomic<uint32_t> g_feed_sequence{0};
std::atomic<uint32_t> g_epoch{0};
std::atomic<bool> g_break_pending{false};
std::atomic<uint64_t> g_settle_until_ms{0};
std::atomic<uint64_t> g_last_block_ms{0};
std::atomic<uint32_t> g_blocks{0}, g_wrong_rate{0}, g_wrong_center{0}, g_settle_skipped{0};
std::atomic<uint32_t> g_loss_events{0}, g_discontinuities{0}, g_publishes{0}, g_publish_skipped{0};
std::atomic<uint32_t> g_max_block_us{0};
std::atomic<uint32_t> g_start_ms{0}, g_tune_ms{0}, g_first_block_ms{0};
std::atomic<uint64_t> g_total_block_us{0}, g_samples{0}, g_lost_samples{0}, g_digest{0};
std::atomic<uint32_t> g_type_counts[kTypeSlots];
std::atomic<uint32_t> g_stations{0};
std::atomic<bool> g_test_replay{false};
// CRC-valid frames the field parser then refused (a bad type or length). Over noise a 16-bit CRC passes about one candidate in
// 65536, and those frames never parse, so they are counted here and kept out of the vessel list, journal and CRC counters.
std::atomic<uint32_t> g_parse_refused[2];
// UI-task state.
Link g_link = Link::stopped;
bool g_owns_rate = false, g_want_tune = false, g_ever_flowed = false;
uint32_t g_tune_calls = 0, g_tune_refused = 0, g_alloc_failed = 0, g_refused_seen = 0;
uint64_t g_last_tune_try_ms = 0, g_stuck_since_ms = 0;

uint64_t now_ms() { return g_hooks.now_ms ? g_hooks.now_ms() : 0; }

void* allocate(size_t bytes) {
#if defined(ESP_PLATFORM)
  // PSRAM only. The internal pool belongs to Wi-Fi, audio and USB; a 70 KB fallback there would starve them.
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}
void release_memory(void* memory) {
#if defined(ESP_PLATFORM)
  heap_caps_free(memory);
#else
  std::free(memory);
#endif
}

bool lock_feed() { return !g_feed_lock.test_and_set(std::memory_order_acquire); }
void unlock_feed() { g_feed_lock.clear(std::memory_order_release); }

void format_position(char* out, size_t size, int32_t lat_e7, int32_t lon_e7) {
  const double lat = lat_e7 / 1e7, lon = lon_e7 / 1e7;
  std::snprintf(out, size, "%.4f%c %.4f%c", lat < 0 ? -lat : lat, lat < 0 ? 'S' : 'N', lon < 0 ? -lon : lon,
                lon < 0 ? 'W' : 'E');
}

// DSP task, inline for every CRC-valid packet: newest entry first in the bounded journal.
void on_packet(const ais_rx::Packet& packet, const ais::Message* parsed, void* context) {
  State* state = static_cast<State*>(context);
  g_digest.fetch_add(packet_hash(packet), std::memory_order_relaxed);
  const uint8_t type = parsed ? parsed->type : 0;
  g_type_counts[type < kTypeSlots ? type : 0].fetch_add(1, std::memory_order_relaxed);
  if (!parsed) {
    g_parse_refused[packet.channel == 2 ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
    return;
  }

  ais_ui::Snapshot& work = state->work;
  const size_t keep = ais_ui::kMessages - 1;
  std::memmove(&work.messages[1], &work.messages[0], keep * sizeof(work.messages[0]));
  std::memmove(&state->message_rx_ms[1], &state->message_rx_ms[0], keep * sizeof(state->message_rx_ms[0]));
  if (work.message_count < ais_ui::kMessages) ++work.message_count;
  state->message_rx_ms[0] = now_ms();

  ais_ui::Message m{};
  m.channel = packet.channel;
  m.crc_ok = true;                                      // only FCS-valid frames reach this point
  m.type = parsed ? parsed->type : static_cast<uint8_t>(packet.bit_count >= 6 ? packet.bytes[0] >> 2 : 0);
  if (parsed) {
    if (parsed->type == 28) { m.identity_kind = ais_ui::IdentityKind::source_id28; m.source_id28 = parsed->source_id; }
    else m.mmsi = parsed->source_id;
    if (parsed->has_position) {
      char where[40];
      format_position(where, sizeof(where), parsed->latitude_e7, parsed->longitude_e7);
      if (parsed->has_speed)
        std::snprintf(m.summary, sizeof(m.summary), "%s %u.%ukn", where,
                      static_cast<unsigned>(parsed->speed_tenths_kn / 10), static_cast<unsigned>(parsed->speed_tenths_kn % 10));
      else
        std::snprintf(m.summary, sizeof(m.summary), "%s", where);
    } else if (parsed->coverage == ais::Coverage::static_voyage) {
      std::snprintf(m.summary, sizeof(m.summary), "%s / %s", parsed->vessel.has_name ? parsed->vessel.name : "NO NAME",
                    parsed->vessel.has_destination ? parsed->vessel.destination : "NO DESTINATION");
    } else if (parsed->coverage == ais::Coverage::static_b) {
      if (parsed->vessel.part == 0)
        std::snprintf(m.summary, sizeof(m.summary), "PART A %s", parsed->vessel.has_name ? parsed->vessel.name : "NO NAME");
      else
        std::snprintf(m.summary, sizeof(m.summary), "PART B CALL %s",
                      parsed->vessel.has_callsign ? parsed->vessel.callsign : "NOT SENT");
    } else if (parsed->coverage == ais::Coverage::header_only) {
      std::snprintf(m.summary, sizeof(m.summary), "%u BITS / FIELDS NOT DECODED", static_cast<unsigned>(packet.bit_count));
    } else {
      std::snprintf(m.summary, sizeof(m.summary), "%u BITS / NO POSITION", static_cast<unsigned>(packet.bit_count));
    }
  } else {
    std::snprintf(m.summary, sizeof(m.summary), "%u BITS / LENGTH NOT VALID FOR TYPE", static_cast<unsigned>(packet.bit_count));
  }
  const size_t bytes = (packet.bit_count + 7u) / 8u;
  const size_t fit = (sizeof(m.raw_hex) - 1) / 2;
  static const char kHex[] = "0123456789ABCDEF";
  size_t at = 0;
  for (size_t i = 0; i < bytes && i < fit; ++i) {
    m.raw_hex[at++] = kHex[packet.bytes[i] >> 4];
    m.raw_hex[at++] = kHex[packet.bytes[i] & 15u];
  }
  m.raw_hex[at] = '\0';
  m.raw_truncated = bytes > fit;
  work.messages[0] = m;
}

uint32_t clamp32(uint64_t value) { return value > 0xffffffffull ? 0xffffffffu : static_cast<uint32_t>(value); }

// DSP task. Builds the UI copy from the mutable store here, on its owner, then hands it over without blocking.
void publish(State& state, uint64_t now) {
  state.pipeline.prune(now, kPruneIdleMs);
  ais_ui::Snapshot& work = state.work;
  ais_ui::populate_targets(state.pipeline.stations(), now, kPositionStaleMs, &work);
  for (size_t i = 0; i < work.message_count; ++i)
    work.messages[i].age_ms = clamp32(now - state.message_rx_ms[i]);
  const ais_rx::PipelineStats stats = state.pipeline.stats();
  for (unsigned channel = 0; channel < 2; ++channel) {
    work.frames[channel] = clamp32(stats.rf.channels[channel].candidate_frames);
    const uint64_t refused = g_parse_refused[channel].load(std::memory_order_relaxed);
    const uint64_t ok = stats.rf.channels[channel].crc_ok;
    work.crc_ok[channel] = clamp32(ok > refused ? ok - refused : 0);   // CRC-valid AND parsed as an AIS message
    work.crc_failed[channel] = clamp32(stats.rf.channels[channel].crc_failed);
  }
  work.iq_drops = clamp32(g_lost_samples.load(std::memory_order_relaxed));
  work.sample_rate_sps = kStreamRate;
  work.elapsed_s = clamp32((now - state.started_ms) / 1000u);
  g_stations.store(static_cast<uint32_t>(state.pipeline.stations().size()), std::memory_order_relaxed);
  if (!lock_feed()) {            // the UI is copying: it gets the next one
    g_publish_skipped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  state.shared = work;
  state.shared_stats = stats;
  unlock_feed();
  g_feed_sequence.fetch_add(1, std::memory_order_release);
  g_publishes.fetch_add(1, std::memory_order_relaxed);
}

bool device_ready() { return g_hooks.device_ready && g_hooks.device_ready(); }

void try_tune(uint64_t now) {
  g_last_tune_try_ms = now;
  ++g_tune_calls;
  const uint64_t previous_settle = g_settle_until_ms.load(std::memory_order_relaxed);
  g_settle_until_ms.store(now + kSettleMs, std::memory_order_release);
  const bool accepted = g_hooks.tune && g_hooks.tune(kCenterHz, kStreamRate);
  if (accepted) {
    g_tune_ms.store(static_cast<uint32_t>(now), std::memory_order_relaxed);
    g_epoch.fetch_add(1, std::memory_order_release);     // nothing decoded before this tune may join what follows
    g_owns_rate = true;
    g_want_tune = false;
  } else {
    g_settle_until_ms.store(previous_settle, std::memory_order_release);
    ++g_tune_refused;
    g_want_tune = true;
  }
}

void reset_counters() {
  g_blocks = 0; g_wrong_rate = 0; g_wrong_center = 0; g_settle_skipped = 0;
  g_loss_events = 0; g_discontinuities = 0; g_publishes = 0; g_publish_skipped = 0;
  g_max_block_us = 0; g_total_block_us = 0; g_samples = 0; g_lost_samples = 0; g_digest = 0;
  for (auto& count : g_type_counts) count = 0;
  g_stations = 0; g_last_block_ms = 0; g_feed_sequence = 0;
  g_parse_refused[0] = 0; g_parse_refused[1] = 0;
  g_tune_calls = g_tune_refused = g_refused_seen = 0;
  g_stuck_since_ms = 0; g_ever_flowed = false;
}

// Waits (bounded) for a block still inside offer_iq. False = the DSP task is still in there.
bool wait_offer_idle() {
  const uint64_t from = now_ms();
  while (g_in_offer.load(std::memory_order_acquire)) {
    if (now_ms() - from > 250u) return false;
  }
  return true;
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

uint64_t packet_hash(const ais_rx::Packet& packet) {
  uint64_t hash = 1469598103934665603ull;               // FNV-1a 64
  auto mix = [&hash](uint8_t byte) { hash ^= byte; hash *= 1099511628211ull; };
  mix(packet.channel);
  mix(static_cast<uint8_t>(packet.bit_count & 0xffu));
  mix(static_cast<uint8_t>(packet.bit_count >> 8));
  const size_t bytes = (packet.bit_count + 7u) / 8u;
  for (size_t i = 0; i < bytes && i < ais_rx::kMaxPayloadBytes; ++i) mix(packet.bytes[i]);
  return hash;
}

void install(const Hooks& hooks) { g_hooks = hooks; }

bool start() {
  if (!g_hooks.device_ready || !g_hooks.tune || !g_hooks.now_ms) return false;
  if (g_running.load(std::memory_order_acquire)) return true;   // re-opening must not reset a running receiver
  if (!wait_offer_idle()) return false;
  if (!g_state) {
    void* memory = allocate(sizeof(State));
    if (!memory) {
      g_alloc_failed = static_cast<uint32_t>(sizeof(State));
      g_link = Link::no_memory;
      return false;
    }
    g_state = new (memory) State();
  }
  reset_counters();
  const uint64_t now = now_ms();
  g_state->pipeline.clear();
  g_state->pipeline.set_packet_observer(on_packet, g_state);   // clear() drops observers: set them after it
  g_state->started_ms = now;
  g_start_ms.store(static_cast<uint32_t>(now), std::memory_order_relaxed);
  g_first_block_ms.store(0, std::memory_order_relaxed);
  g_break_pending.store(false, std::memory_order_relaxed);
  g_link = Link::tuning;
  g_running.store(true, std::memory_order_release);
  try_tune(now);
  return true;
}

void stop() {
  const bool was_running = g_running.exchange(false, std::memory_order_acq_rel);
  if (g_owns_rate && g_hooks.release) g_hooks.release();
  g_owns_rate = false;
  g_want_tune = false;
  g_test_replay.store(false, std::memory_order_relaxed);
  if (was_running || g_link != Link::no_memory) g_link = Link::stopped;
  if (g_state && wait_offer_idle()) {      // if a block is somehow still inside, keep the memory; the next start reuses it
    g_state->~State();
    release_memory(g_state);
    g_state = nullptr;
  }
}

bool running() { return g_running.load(std::memory_order_acquire); }

void set_test_replay(bool enabled) { g_test_replay.store(enabled, std::memory_order_relaxed); }

void note_discontinuity() {
  g_break_pending.store(true, std::memory_order_release);
  g_discontinuities.fetch_add(1, std::memory_order_relaxed);
}

void offer_iq(const uint8_t* cu8, size_t bytes, uint32_t sample_rate) {
  if (!g_running.load(std::memory_order_acquire)) return;
  g_in_offer.store(true);
  struct Leave { ~Leave() { g_in_offer.store(false, std::memory_order_release); } } leave_guard;
  if (!g_running.load() || !g_state) return;             // stop() ran between the check and the flag
  State& state = *g_state;
  if (sample_rate != kStreamRate) {                      // never feed another rate to a 240 kS/s decoder
    g_wrong_rate.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (g_hooks.tuned_center && g_hooks.tuned_center() != kCenterHz) {
    g_wrong_center.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const uint64_t now = now_ms();
  if (now < g_settle_until_ms.load(std::memory_order_acquire)) {
    g_settle_skipped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const uint32_t t0 = g_hooks.micros ? g_hooks.micros() : 0;
  // Sample continuity: any upstream loss or reconnect starts a new epoch, so the pipeline never joins samples across it.
  if (g_hooks.lost_samples) {
    const uint64_t lost = g_hooks.lost_samples();
    if (!state.lost_primed) { state.lost_primed = true; state.lost_baseline = state.lost_seen = lost; }
    if (lost != state.lost_seen) {
      state.lost_seen = lost;
      g_lost_samples.store(lost - state.lost_baseline, std::memory_order_relaxed);
      g_loss_events.fetch_add(1, std::memory_order_relaxed);
      g_epoch.fetch_add(1, std::memory_order_release);
    }
  }
  if (g_break_pending.exchange(false, std::memory_order_acq_rel)) g_epoch.fetch_add(1, std::memory_order_release);
  uint32_t epoch = g_epoch.load(std::memory_order_acquire);
  if (epoch == 0) epoch = 1;                             // the pipeline treats 0 as "no session"
  state.offered_epoch = epoch;
  (void)state.pipeline.offer(cu8, bytes, sample_rate, kCenterHz, epoch, now);
  g_last_block_ms.store(now, std::memory_order_release);
  if (g_blocks.load(std::memory_order_relaxed) == 0) g_first_block_ms.store(static_cast<uint32_t>(now), std::memory_order_relaxed);
  g_blocks.fetch_add(1, std::memory_order_relaxed);
  g_samples.fetch_add(bytes / 2, std::memory_order_relaxed);
  if (now - state.last_publish_ms >= kPublishMs) {
    state.last_publish_ms = now;
    publish(state, now);
  }
  if (g_hooks.micros) {
    const uint32_t elapsed = g_hooks.micros() - t0;
    g_total_block_us.fetch_add(elapsed, std::memory_order_relaxed);
    if (elapsed > g_max_block_us.load(std::memory_order_relaxed)) g_max_block_us.store(elapsed, std::memory_order_relaxed);
  }
}

void service() {
  if (!g_running.load(std::memory_order_acquire)) return;
  const uint64_t now = now_ms();
  const bool ready = device_ready();
  const uint64_t last_block = g_last_block_ms.load(std::memory_order_acquire);
  const bool flowing = last_block != 0 && now - last_block < kFlowTimeoutMs;
  if (flowing) g_ever_flowed = true;
  const uint32_t refused = g_wrong_rate.load(std::memory_order_relaxed) + g_wrong_center.load(std::memory_order_relaxed);
  const bool refused_moving = refused != g_refused_seen;
  g_refused_seen = refused;

  if (!ready) g_link = Link::no_receiver;
  else if (flowing) g_link = Link::receiving;
  else if (refused_moving || g_stuck_since_ms != 0) g_link = Link::wrong_stream;
  else g_link = g_ever_flowed ? Link::stalled : Link::tuning;

  if (!ready) { g_want_tune = true; g_stuck_since_ms = 0; return; }   // ask again as soon as a receiver is back
  if (g_want_tune && now - g_last_tune_try_ms >= kRetuneRetryMs) { try_tune(now); return; }
  // Self-heal: the stream was moved to another rate or centre behind our back (a replug restarts it at the band default,
  // another screen may retune it). Refused blocks keep arriving with no good one: ask for our tune again.
  if (flowing) g_stuck_since_ms = 0;
  else if (refused_moving && g_stuck_since_ms == 0) g_stuck_since_ms = now;
  if (g_stuck_since_ms != 0 && now - g_stuck_since_ms >= kStuckRetuneMs && now - g_last_tune_try_ms >= kRetuneRetryMs) {
    try_tune(now);
    g_stuck_since_ms = now;
  }
}

bool copy_feed(ais_ui::Snapshot* out, uint32_t* sequence) {
  if (!out || !sequence || !g_state) return false;
  const uint32_t published = g_feed_sequence.load(std::memory_order_acquire);
  if (published == *sequence) return false;
  if (!lock_feed()) return false;
  const ais_ui::Snapshot& from = g_state->shared;
  std::memcpy(out->targets, from.targets, sizeof(out->targets));
  std::memcpy(out->messages, from.messages, sizeof(out->messages));
  std::memcpy(out->events, from.events, sizeof(out->events));
  out->target_count = from.target_count;
  out->message_count = from.message_count;
  out->event_count = from.event_count;
  for (unsigned channel = 0; channel < 2; ++channel) {
    out->frames[channel] = from.frames[channel];
    out->crc_ok[channel] = from.crc_ok[channel];
    out->crc_failed[channel] = from.crc_failed[channel];
  }
  out->iq_drops = from.iq_drops;
  out->sample_rate_sps = from.sample_rate_sps;
  out->elapsed_s = from.elapsed_s;
  unlock_feed();
  out->test_replay = g_test_replay.load(std::memory_order_relaxed);
  *sequence = published;
  return true;
}

Status status() {
  Status s;
  s.link = g_link;
  s.running = g_running.load(std::memory_order_acquire);
  s.remote = g_hooks.remote_source && g_hooks.remote_source();
  s.test_replay = g_test_replay.load(std::memory_order_relaxed);
  s.epoch = g_epoch.load(std::memory_order_relaxed);
  s.blocks = g_blocks.load(std::memory_order_relaxed);
  s.wrong_rate_blocks = g_wrong_rate.load(std::memory_order_relaxed);
  s.wrong_center_blocks = g_wrong_center.load(std::memory_order_relaxed);
  s.settle_skipped = g_settle_skipped.load(std::memory_order_relaxed);
  s.loss_events = g_loss_events.load(std::memory_order_relaxed);
  s.discontinuities = g_discontinuities.load(std::memory_order_relaxed);
  s.tune_calls = g_tune_calls;
  s.tune_refused = g_tune_refused;
  s.publishes = g_publishes.load(std::memory_order_relaxed);
  s.publish_skipped = g_publish_skipped.load(std::memory_order_relaxed);
  s.max_block_us = g_max_block_us.load(std::memory_order_relaxed);
  s.start_ms = g_start_ms.load(std::memory_order_relaxed);
  s.tune_ms = g_tune_ms.load(std::memory_order_relaxed);
  s.first_block_ms = g_first_block_ms.load(std::memory_order_relaxed);
  s.avg_block_us = s.blocks ? static_cast<uint32_t>(g_total_block_us.load(std::memory_order_relaxed) / s.blocks) : 0;
  s.samples = g_samples.load(std::memory_order_relaxed);
  s.lost_samples = g_lost_samples.load(std::memory_order_relaxed);
  s.packet_digest = g_digest.load(std::memory_order_relaxed);
  for (uint32_t i = 0; i < kTypeSlots; ++i) s.type_counts[i] = g_type_counts[i].load(std::memory_order_relaxed);
  s.stations = g_stations.load(std::memory_order_relaxed);
  s.state_bytes = g_state ? static_cast<uint32_t>(sizeof(State)) : 0;
  s.alloc_failed_bytes = g_alloc_failed;
  if (g_state && lock_feed()) {
    s.pipeline = g_state->shared_stats;
    unlock_feed();
  }
  return s;
}

void bench(char* out, size_t size) {
  if (!out || size == 0) return;
  if (!g_hooks.micros) { std::snprintf(out, size, "RTL_AIS_BENCH error=no_clock"); return; }
  constexpr size_t kBlock = 48000;                       // 0.1 s of IQ
  constexpr unsigned kBlocks = 10;                       // 1 s in total
  void* memory = allocate(sizeof(ais_rx::Receiver) + kBlock);
  if (!memory) { std::snprintf(out, size, "RTL_AIS_BENCH error=no_memory"); return; }
  ais_rx::Receiver* receiver = new (memory) ais_rx::Receiver();
  uint8_t* iq = static_cast<uint8_t*>(memory) + sizeof(ais_rx::Receiver);
  uint32_t seed = 0x13579bdfu;
  for (size_t i = 0; i < kBlock; ++i) {                  // receiver noise: a few LSB around mid-scale
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    iq[i] = static_cast<uint8_t>(120u + (seed & 15u));
  }
  const uint32_t t0 = g_hooks.micros();
  for (unsigned i = 0; i < kBlocks; ++i) (void)receiver->process_cu8(iq, kBlock, kStreamRate, nullptr, nullptr);
  const uint32_t whole_us = g_hooks.micros() - t0;
  const ais_rx::Stats stats = receiver->stats();
  // The same amount of each stage on its own (same loop shapes as the receiver), to show where the time goes.
  float taps[39], line[78];
  for (unsigned i = 0; i < 39; ++i) taps[i] = 1.f / 39.f;
  for (unsigned i = 0; i < 78; ++i) line[i] = static_cast<float>(iq[i]) * (1.f / 128.f) - 1.f;
  volatile float sink = 0;
  // FIR: four partial sums per rail, as in Receiver::demodulate (192,000 outputs of 39 taps).
  const uint32_t t1 = g_hooks.micros();
  for (unsigned n = 0; n < 2u * 2u * 48000u; ++n) {
    const float* x = &line[n % 39u];
    float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    unsigned k = 0;
    for (; k + 4 <= 39; k += 4) { a0 += taps[k] * x[k]; a1 += taps[k + 1] * x[k + 1]; a2 += taps[k + 2] * x[k + 2]; a3 += taps[k + 3] * x[k + 3]; }
    for (; k < 39; ++k) a0 += taps[k] * x[k];
    sink = sink + ((a0 + a1) + (a2 + a3));
  }
  const uint32_t fir_us = g_hooks.micros() - t1;
  // Candidate replacements for the filter, same 192,000 outputs of 39 taps each.
  //  symmetric float: the taps are symmetric, so fold the two halves and multiply once per pair
  const uint32_t t5 = g_hooks.micros();
  for (unsigned n = 0; n < 2u * 2u * 48000u; ++n) {
    const float* x = &line[n % 39u];
    float a0 = 0, a1 = 0;
    unsigned k = 0;
    for (; k + 2 <= 19; k += 2) { a0 += taps[k] * (x[k] + x[38 - k]); a1 += taps[k + 1] * (x[k + 1] + x[37 - k]); }
    for (; k < 19; ++k) a0 += taps[k] * (x[k] + x[38 - k]);
    sink = sink + (a0 + a1 + taps[19] * x[19]);
  }
  const uint32_t fir_sym_us = g_hooks.micros() - t5;
  //  symmetric int32: 16-bit taps and samples, integer multiply-accumulate
  int16_t itaps[64], iline[78];
  for (unsigned i = 0; i < 64; ++i) itaps[i] = static_cast<int16_t>(500 + i);
  for (unsigned i = 0; i < 78; ++i) iline[i] = static_cast<int16_t>(static_cast<int>(iq[i]) - 128);
  volatile int32_t isink = 0;
  const uint32_t t6 = g_hooks.micros();
  for (unsigned n = 0; n < 2u * 2u * 48000u; ++n) {
    const int16_t* x = &iline[n % 39u];
    int32_t a0 = 0, a1 = 0;
    unsigned k = 0;
    for (; k + 2 <= 19; k += 2) { a0 += itaps[k] * (x[k] + x[38 - k]); a1 += itaps[k + 1] * (x[k + 1] + x[37 - k]); }
    for (; k < 19; ++k) a0 += itaps[k] * (x[k] + x[38 - k]);
    isink = isink + ((a0 + a1 + itaps[19] * x[19]) >> 8);
  }
  const uint32_t fir_int_us = g_hooks.micros() - t6;
  //  fast atan2 (polynomial, about 1e-4 rad)
  const uint32_t t7 = g_hooks.micros();
  for (unsigned n = 0; n < 2u * 48000u; ++n) {
    const float y = line[n % 78u], xx = line[(n + 19u) % 78u];
    const float ax = std::fabs(xx), ay = std::fabs(y);
    const float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    float r = 0;
    if (mx > 0) { const float a = mn / mx, s = a * a; r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a; }
    if (ay > ax) r = 1.57079637f - r;
    if (xx < 0) r = 3.14159274f - r;
    if (y < 0) r = -r;
    sink = sink + r;
  }
  const uint32_t atan_fast_us = g_hooks.micros() - t7;
  // HDLC bit processing: 2 slicer hypotheses x 96,000 channel outputs per second of IQ.
  ais_rx::Hdlc hdlc;
  uint32_t lfsr = 0xace1u;
  const uint32_t t4 = g_hooks.micros();
  for (unsigned n = 0; n < 192000u; ++n) { lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u); hdlc.process_level(lfsr & 1u, 1, nullptr, nullptr); }
  const uint32_t hdlc_us = g_hooks.micros() - t4;
  const uint32_t t2 = g_hooks.micros();
  for (unsigned n = 0; n < 2u * 48000u; ++n)
    sink = sink + std::atan2(line[n % 78u], line[(n + 19u) % 78u]);
  const uint32_t atan_us = g_hooks.micros() - t2;
  receiver->~Receiver();
  release_memory(memory);
  std::snprintf(out, size,
                "RTL_AIS_BENCH iq_seconds=1 receiver_us=%u duty_pct=%u.%u fir_us=%u fir_sym_us=%u fir_int_us=%u atan2_us=%u atan2_fast_us=%u hdlc_us=%u samples=%llu "
                "candidate_frames=%llu",
                static_cast<unsigned>(whole_us), static_cast<unsigned>(whole_us / 10000u),
                static_cast<unsigned>((whole_us / 1000u) % 10u), static_cast<unsigned>(fir_us), static_cast<unsigned>(fir_sym_us),
                static_cast<unsigned>(fir_int_us), static_cast<unsigned>(atan_us), static_cast<unsigned>(atan_fast_us), static_cast<unsigned>(hdlc_us),
                static_cast<unsigned long long>(stats.iq_samples),
                static_cast<unsigned long long>(stats.channels[0].candidate_frames + stats.channels[1].candidate_frames));
}

void format_status(char* out, size_t size) {
  if (!out || size == 0) return;
  const Status s = status();
  const auto& c1 = s.pipeline.rf.channels[0];
  const auto& c2 = s.pipeline.rf.channels[1];
  int at = std::snprintf(
      out, size,
      "RTL_AIS_STATUS running=%d link=%s source=%s test=%d epoch=%u blocks=%u samples=%llu wrong_rate=%u wrong_center=%u "
      "settle_skipped=%u lost_samples=%llu loss_events=%u discontinuities=%u tune_calls=%u tune_refused=%u "
      "ch1_ok=%llu ch1_fail=%llu ch2_ok=%llu ch2_fail=%llu dup=%llu packets=%llu decoded=%llu header_only=%llu parse_rejected=%llu "
      "station_updates=%llu station_rejected=%llu stations=%u gen_changes=%llu iq_gaps=%llu input_refused=%llu "
      "digest=%016llx publishes=%u publish_skipped=%u avg_block_us=%u max_block_us=%u state_bytes=%u alloc_failed=%u "
      "start_ms=%u tune_ms=%u first_block_ms=%u types=",
      s.running ? 1 : 0, link_name(s.link), s.remote ? "rtl_tcp" : "usb", s.test_replay ? 1 : 0, static_cast<unsigned>(s.epoch),
      static_cast<unsigned>(s.blocks), static_cast<unsigned long long>(s.samples), static_cast<unsigned>(s.wrong_rate_blocks),
      static_cast<unsigned>(s.wrong_center_blocks), static_cast<unsigned>(s.settle_skipped),
      static_cast<unsigned long long>(s.lost_samples), static_cast<unsigned>(s.loss_events),
      static_cast<unsigned>(s.discontinuities), static_cast<unsigned>(s.tune_calls), static_cast<unsigned>(s.tune_refused),
      static_cast<unsigned long long>(c1.crc_ok), static_cast<unsigned long long>(c1.crc_failed),
      static_cast<unsigned long long>(c2.crc_ok), static_cast<unsigned long long>(c2.crc_failed),
      static_cast<unsigned long long>(c1.duplicates + c2.duplicates), static_cast<unsigned long long>(s.pipeline.packets),
      static_cast<unsigned long long>(s.pipeline.messages_decoded), static_cast<unsigned long long>(s.pipeline.messages_header_only),
      static_cast<unsigned long long>(s.pipeline.parse_rejected), static_cast<unsigned long long>(s.pipeline.station_updates),
      static_cast<unsigned long long>(s.pipeline.station_rejected), static_cast<unsigned>(s.stations),
      static_cast<unsigned long long>(s.pipeline.generation_changes), static_cast<unsigned long long>(s.pipeline.iq_gaps),
      static_cast<unsigned long long>(s.pipeline.input_refused), static_cast<unsigned long long>(s.packet_digest),
      static_cast<unsigned>(s.publishes), static_cast<unsigned>(s.publish_skipped), static_cast<unsigned>(s.avg_block_us),
      static_cast<unsigned>(s.max_block_us), static_cast<unsigned>(s.state_bytes), static_cast<unsigned>(s.alloc_failed_bytes),
      static_cast<unsigned>(s.start_ms), static_cast<unsigned>(s.tune_ms), static_cast<unsigned>(s.first_block_ms));
  bool any = false;
  for (uint32_t type = 0; type < kTypeSlots && at > 0 && static_cast<size_t>(at) + 16 < size; ++type) {
    if (!s.type_counts[type]) continue;
    at += std::snprintf(out + at, size - static_cast<size_t>(at), "%s%u:%u", any ? "," : "", static_cast<unsigned>(type),
                        static_cast<unsigned>(s.type_counts[type]));
    any = true;
  }
  if (!any && at > 0 && static_cast<size_t>(at) + 2 < size) std::snprintf(out + at, size - static_cast<size_t>(at), "-");
}

}  // namespace orcsdr::ais_radio

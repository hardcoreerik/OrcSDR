#include "acars_receiver.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "acars_core.hpp"

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

struct Rx {
  acars::Frontend240k front;
  acars::Decoder decoder;
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

// UI-task state.
uint32_t g_wanted_hz = 0;         // the channel the dashboard asked for (the decoder is only relabelled once the radio accepts it)
bool g_want_tune = false;        // a channel is wanted and the radio has not accepted it yet
bool g_owns_rate = false;        // tune() was accepted: release() must be called
bool g_reported = false;         // the model has been told the outcome of the current request
uint32_t g_tune_calls = 0;
uint32_t g_tune_refused = 0;
uint32_t g_last_tune_try_ms = 0;

uint32_t now_ms() { return g_hooks.now_ms ? g_hooks.now_ms() : 0u; }

#if defined(ESP_PLATFORM)
uint32_t micros32() { return static_cast<uint32_t>(esp_timer_get_time()); }
#else
uint32_t micros32() { return 0; }
#endif

void publish_stats() {
  if (!g_rx || !g_model) return;
  const acars::Stats& s = g_rx->decoder.stats();
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
  const uint32_t center = channel - static_cast<uint32_t>(kCarrierOffsetHz);
  // Blocks already in flight belong to the previous centre; the DSP task drops them for a moment, then re-seeds the decoder.
  // The settle window opens before the request (the radio may retune immediately) and is withdrawn if the request is refused.
  const uint32_t previous_settle = g_settle_until_ms.load(std::memory_order_relaxed);
  g_settle_until_ms.store(now + kSettleMs, std::memory_order_release);
  const bool accepted = g_hooks.tune && g_hooks.tune(center);
  if (accepted) {
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
  if (sample_rate != kStreamRate) {
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
  if (g_apply_pending.exchange(false, std::memory_order_acq_rel)) {
    g_rx->front.reset();
    g_rx->decoder.reset_stream();
    g_rx->decoder.set_frequency(g_channel_hz.load(std::memory_order_relaxed));
  } else if (last != 0 && now - last > kGapResetMs) {
    g_rx->front.reset();                                    // never bridge missing samples
    g_rx->decoder.reset_stream();                           // counters keep counting
    g_discontinuities.fetch_add(1, std::memory_order_relaxed);
  }
  g_last_block_ms.store(now, std::memory_order_release);
  g_blocks.fetch_add(1, std::memory_order_relaxed);
  g_rx->front.process_cu8(cu8, bytes / 2, on_sample, g_rx);
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
  return s;
}

void format_status(char* out, size_t size) {
  if (!out || size == 0) return;
  const Status s = status();
  std::snprintf(out, size,
                "RTL_ACARS_STATUS running=%d channel_hz=%u center_hz=%u blocks=%u wrong_rate=%u settle_skipped=%u gaps=%u "
                "tune_calls=%u tune_refused=%u msgs=%u msg_drops=%u max_block_us=%u avg_block_us=%u level=%.3f sync=%u crc_ok=%u corrected=%u "
                "crc_rej=%u framing_rej=%u overflow=%u bits=%u",
                s.running ? 1 : 0, static_cast<unsigned>(s.channel_hz), static_cast<unsigned>(s.center_hz),
                static_cast<unsigned>(s.blocks), static_cast<unsigned>(s.wrong_rate_blocks),
                static_cast<unsigned>(s.settle_skipped), static_cast<unsigned>(s.discontinuities),
                static_cast<unsigned>(s.tune_calls), static_cast<unsigned>(s.tune_refused),
                static_cast<unsigned>(s.msgs_posted), static_cast<unsigned>(s.msgs_dropped),
                static_cast<unsigned>(s.max_block_us), static_cast<unsigned>(s.avg_block_us), static_cast<double>(s.level),
                static_cast<unsigned>(s.decoder.sync_candidates), static_cast<unsigned>(s.decoder.crc_ok),
                static_cast<unsigned>(s.decoder.corrected), static_cast<unsigned>(s.decoder.crc_rejected),
                static_cast<unsigned>(s.decoder.framing_rejected), static_cast<unsigned>(s.decoder.overflows),
                static_cast<unsigned>(s.decoder.bits));
}

}  // namespace orcsdr::acars_receiver

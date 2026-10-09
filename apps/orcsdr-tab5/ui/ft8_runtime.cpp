#include "ft8_runtime.hpp"
#include "js8_native_backend.hpp"

#include "ft8_audio_tap.hpp"
#include "ft8_native_backend.hpp"
#include "ft8_spectral_fft.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <sys/time.h>

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace orcsdr::ft8_runtime {
namespace {

using orcsdr::ftx::Mode;
namespace tap_ns = orcsdr::ftx::audio_tap;

constexpr size_t kRingSamples = 12000u * 32u;    // 32 s of 12 kS/s audio, PSRAM
constexpr size_t kDecodeCapacity = 24;
constexpr uint32_t kStartDelayMs = 400;          // ring must hold the last samples of a slot before it is cut

struct Runtime {
  std::atomic<bool> active{false};
  std::atomic<bool> task_alive{false};
  std::atomic<bool> wf_alive{false};
  uint8_t* wf = nullptr;
  std::atomic<uint32_t> wf_sequence{0};
  TaskHandle_t wf_task = nullptr;
  std::atomic<bool> reset_pending{false};
  std::atomic<uint64_t> total{0};                // 12 kS/s samples written to the ring
  std::atomic<uint64_t> cont_ms{0};              // wall time of the last discontinuity (tap start / reset)
  std::atomic<uint64_t> cont_total{0};           // ring total at that moment
  std::atomic<uint32_t> blocks_us_sum{0};
  std::atomic<uint32_t> blocks_us_n{0};
  std::atomic<uint32_t> max_block_us{0};
  std::atomic<uint64_t> tap_blocks{0};
  std::atomic<uint8_t> state{static_cast<uint8_t>(State::stopped)};
  std::atomic<bool> mode_changed{false};
  std::atomic<int64_t> last_touch_us{0};
  std::atomic<bool> headless{false};
  std::atomic<bool> config_dirty{false};
  uint16_t cfg_k = 64;
  uint16_t cfg_gate = 32;
  uint8_t cfg_fine_rows = 4;
  uint32_t cfg_deadline_ms = 11000;
  bool have_slot = false;
  int16_t* inject = nullptr;                  // test recording
  size_t inject_count = 0;
  size_t inject_capacity = 0;
  size_t inject_written = 0;
  std::atomic<bool> inject_pending{false};
  orcsdr::ft8::DigitalMode inject_mode = orcsdr::ft8::DigitalMode::ft8;
  int16_t* snapshot = nullptr;                 // stable copy of the last decoded slot (for DUMP / SAVE)
  size_t snapshot_count = 0;
  uint64_t snapshot_slot_ms = 0;
  std::atomic<bool> snapshot_hold{false};

  int16_t* ring = nullptr;
  orcsdr::ftx::native::Backend* backend = nullptr;
  orcsdr::js8::native::Backend* js8 = nullptr;   // allocated on first use of a JS8 mode
  Js8Stats js8_stats{};
  Js8Raw js8_raw[16]{};
  std::atomic<uint8_t> js8_raw_count{0};
  tap_ns::Tap tap{};
  bool tap_ready = false;
  uint32_t input_rate = 0;

  orcsdr::ft8::DigitalMode mode = orcsdr::ft8::DigitalMode::ft8;
  DecodeCallback on_decode = nullptr;
  void* context = nullptr;
  ClockValidFn clock_valid = nullptr;
  DialOffsetFn dial_offset_fn = nullptr;
  std::atomic<float> dial_offset_hz{0.0f};
  TaskHandle_t task = nullptr;

  Status status{};
};

Runtime g;

uint64_t wall_ms() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return static_cast<uint64_t>(tv.tv_sec) * 1000u + static_cast<uint64_t>(tv.tv_usec / 1000);
}

uint64_t now_us_fn() { return static_cast<uint64_t>(esp_timer_get_time()); }

void set_state(State s) { g.state.store(static_cast<uint8_t>(s), std::memory_order_release); }

Mode ftx_mode(orcsdr::ft8::DigitalMode m) { return m == orcsdr::ft8::DigitalMode::ft4 ? Mode::ft4 : Mode::ft8; }

bool is_js8(orcsdr::ft8::DigitalMode m) { return orcsdr::ft8::mode_is_js8(m); }
// Only Normal has an established sync pattern; every other JS8 submode is refused (never mapped to Normal).
bool js8_supported(orcsdr::ft8::DigitalMode m) { return m == orcsdr::ft8::DigitalMode::js8_normal; }

void* psram_alloc(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void psram_free(void* p) { heap_caps_free(p); }

uint32_t slot_ms_for(orcsdr::ft8::DigitalMode m) {
  if (is_js8(m)) return orcsdr::js8::profile(orcsdr::js8::Submode::normal).slot_ms;
  return orcsdr::ftx::profile(ftx_mode(m)).slot_ms;
}

// Allocates and starts the JS8 backend in PSRAM on first use (about 700 KB), so FT8/FT4 users never pay for it.
void log_js8_frames(const char* tag);
bool js8_attach() {
  if (g.js8 != nullptr) return true;
  void* memory = psram_alloc(sizeof(orcsdr::js8::native::Backend));
  if (memory == nullptr) return false;
  auto* backend = new (memory) orcsdr::js8::native::Backend();
  orcsdr::js8::native::Config config;
  config.now_us = now_us_fn;
  config.deadline_ms = 11000;
  orcsdr::js8::native::Memory ram;
  ram.alloc = psram_alloc;
  ram.release = psram_free;
  if (!backend->begin(12000, orcsdr::js8::Submode::normal, config, ram)) {
    backend->~Backend();
    psram_free(memory);
    return false;
  }
  g.js8 = backend;
  return true;
}

void mark_discontinuity() {
  g.cont_ms.store(wall_ms(), std::memory_order_release);
  g.cont_total.store(g.total.load(std::memory_order_acquire), std::memory_order_release);
}

void decode_slot(uint64_t slot_start_ms, uint32_t slot_ms, uint64_t now_ms, uint64_t total_now) {
  const uint64_t ms_since_start = now_ms - slot_start_ms;
  const uint64_t slot_samples = static_cast<uint64_t>(slot_ms) * 12u;
  const int64_t start_total = static_cast<int64_t>(total_now) - static_cast<int64_t>(ms_since_start * 12u);
  Status& s = g.status;

  const uint64_t cont_ms = g.cont_ms.load(std::memory_order_acquire);
  const uint64_t cont_total = g.cont_total.load(std::memory_order_acquire);
  bool incomplete = false;
  if (start_total < 0 || slot_start_ms < cont_ms + 100u || total_now - static_cast<uint64_t>(start_total) > kRingSamples - 12000u) {
    incomplete = true;
  } else if (now_ms - cont_ms > 5000u) {
    // The tap must have delivered samples at the real 12 kS/s since the last discontinuity, or a block was lost.
    const double rate = static_cast<double>(total_now - cont_total) / static_cast<double>(now_ms - cont_ms);
    if (rate < 11.82 || rate > 12.18) incomplete = true;
  }
  if (incomplete) {
    ++s.slots_skipped_incomplete;
    std::printf("ORC_FT8_RT slot_skipped_incomplete start_ms=%llu\n", static_cast<unsigned long long>(slot_start_ms));
    return;
  }

  static int16_t chunk[4096];   // shared by the FT8/FT4 and JS8 paths: internal RAM is tight, so no second buffer
  if (is_js8(g.mode)) {
    // JS8 (receive only): raw sync/tone evidence, never a decode. The JS8 backend returns no Decode records.
    if (!js8_supported(g.mode) || g.js8 == nullptr) return;
    set_state(State::decoding);
    g.js8->begin_slot(slot_start_ms);
    uint64_t jpos = static_cast<uint64_t>(start_total);
    uint64_t jremaining = slot_samples;
    uint64_t jsumsq = 0;
    uint32_t jpeak = 0, jclipped = 0;
    while (jremaining > 0) {
      const size_t take = jremaining > 4096 ? 4096 : static_cast<size_t>(jremaining);
      for (size_t i = 0; i < take; ++i) {
        const int16_t v = g.ring[(jpos + i) % kRingSamples];
        chunk[i] = v;
        const uint32_t a = static_cast<uint32_t>(v < 0 ? -v : v);
        jsumsq += static_cast<uint64_t>(a) * a;
        if (a > jpeak) jpeak = a;
        if (a >= 32767u) ++jclipped;
      }
      g.js8->offer_audio(chunk, take);
      jpos += take;
      jremaining -= take;
    }
    orcsdr::ft8::Decode jout[8];
    const size_t jn = g.js8->finish_slot(jout, sizeof(jout) / sizeof(jout[0]));
    const auto& js = g.js8->stats();
    s.slot_rms = static_cast<uint32_t>(std::sqrt(static_cast<double>(jsumsq) / static_cast<double>(slot_samples)));
    s.slot_peak = jpeak;
    s.slot_clipped = jclipped;
    ++s.slots_decoded;
    if (!g.snapshot_hold.load(std::memory_order_acquire) && g.snapshot != nullptr) {
      std::memcpy(g.snapshot, g.js8->slot_audio(), g.js8->buffered() * sizeof(int16_t));
      g.snapshot_count = g.js8->buffered();
      g.snapshot_slot_ms = slot_start_ms;
      g.have_slot = true;
    }
    s.last_slot_decodes = static_cast<uint32_t>(jn);
    s.last_decode_ms = js.total_ms;
    s.last_spectral_ms = js.spectral_ms;
    s.last_deadline_hit = js.deadline_hit;
    s.last_coarse = js.candidates;
    s.last_strong = js.strong_candidates;
    Js8Stats& out = g.js8_stats;
    out.attached = true;
    out.active = true;
    ++out.slots;
    out.total_ms = js.total_ms;
    out.spectral_ms = js.spectral_ms;
    out.search_ms = js.search_ms;
    out.demod_ms = js.demod_ms;
    out.grid_rows = js.grid_rows;
    out.candidates = js.candidates;
    out.strong_candidates = js.strong_candidates;
    out.raw_frames = js.raw_frames;
    out.best_sync_score = js.best_sync_score;
    out.deadline_hit = js.deadline_hit;
    out.decodes = js.decodes;
    const size_t keep = std::min<size_t>(g.js8->raw_count(), sizeof(g.js8_raw) / sizeof(g.js8_raw[0]));
    g.js8_raw_count.store(0, std::memory_order_release);
    for (size_t i = 0; i < keep; ++i) {
      const auto* r = g.js8->raw(i);
      g.js8_raw[i] = Js8Raw{r->audio_hz, r->dt_ms, r->sync_score, r->sync_hits, r->mean_margin};
    }
    g.js8_raw_count.store(static_cast<uint8_t>(keep), std::memory_order_release);
    std::printf("ORC_JS8_RT slot=%llu decodes=%u raw_frames=%u candidates=%u strong=%u best_sync=%.2f total_ms=%u spectral=%u search=%u demod=%u rows=%u deadline=%d\n",
                static_cast<unsigned long long>(slot_start_ms / 1000u), static_cast<unsigned>(jn), static_cast<unsigned>(js.raw_frames),
                static_cast<unsigned>(js.candidates), static_cast<unsigned>(js.strong_candidates), static_cast<double>(js.best_sync_score),
                static_cast<unsigned>(js.total_ms), static_cast<unsigned>(js.spectral_ms), static_cast<unsigned>(js.search_ms),
                static_cast<unsigned>(js.demod_ms), static_cast<unsigned>(js.grid_rows), js.deadline_hit ? 1 : 0);
    log_js8_frames("ORC_JS8_RT");
    if (g.on_decode != nullptr)
      for (size_t i = 0; i < jn; ++i) g.on_decode(jout[i], g.context);
    set_state(State::ready);
    return;
  }

  set_state(State::decoding);
  g.backend->begin_slot(slot_start_ms);
  uint64_t sumsq = 0;
  uint32_t peak = 0, clipped = 0;
  uint64_t pos = static_cast<uint64_t>(start_total);
  uint64_t remaining = slot_samples;
  while (remaining > 0) {
    const size_t take = remaining > sizeof(chunk) / sizeof(chunk[0]) ? sizeof(chunk) / sizeof(chunk[0]) : static_cast<size_t>(remaining);
    for (size_t i = 0; i < take; ++i) chunk[i] = g.ring[(pos + i) % kRingSamples];
    for (size_t i = 0; i < take; ++i) {
      const int32_t v = chunk[i];
      const uint32_t a = static_cast<uint32_t>(v < 0 ? -v : v);
      sumsq += static_cast<uint64_t>(a) * a;
      if (a > peak) peak = a;
      if (a >= 32767u) ++clipped;
    }
    g.backend->offer_audio(chunk, take);
    pos += take;
    remaining -= take;
  }
  orcsdr::ft8::Decode out[kDecodeCapacity];
  const size_t n = g.backend->finish_slot(out, kDecodeCapacity);
  const auto& st = g.backend->stats();
  s.slot_rms = static_cast<uint32_t>(std::sqrt(static_cast<double>(sumsq) / static_cast<double>(slot_samples)));
  s.slot_peak = peak;
  s.slot_clipped = clipped;
  ++s.slots_decoded;
  if (!g.snapshot_hold.load(std::memory_order_acquire) && g.snapshot != nullptr) {
    std::memcpy(g.snapshot, g.backend->slot_audio(), g.backend->buffered() * sizeof(int16_t));
    g.snapshot_count = g.backend->buffered();
    g.snapshot_slot_ms = slot_start_ms;
    g.have_slot = true;
  }
  s.last_slot_decodes = static_cast<uint32_t>(n);
  s.last_decode_ms = st.total_ms;
  s.last_spectral_ms = st.spectral_ms;
  s.last_refine_ms = st.refine_ms;
  s.last_gate_ms = st.gate_ms;
  s.last_deadline_hit = st.deadline_hit;
  s.last_coarse = st.coarse_candidates;
  s.last_strong = st.strong_candidates;
  std::printf("ORC_FT8_RT slot=%llu decodes=%u total_ms=%u spectral=%u search=%u refine=%u gates=%u coarse=%u attempted=%u deadline=%d\n",
              static_cast<unsigned long long>(slot_start_ms / 1000u), static_cast<unsigned>(n), static_cast<unsigned>(st.total_ms),
              static_cast<unsigned>(st.spectral_ms), static_cast<unsigned>(st.search_ms), static_cast<unsigned>(st.refine_ms),
              static_cast<unsigned>(st.gate_ms), static_cast<unsigned>(st.coarse_candidates), static_cast<unsigned>(st.attempted),
              st.deadline_hit ? 1 : 0);
  if (g.on_decode != nullptr)
    for (size_t i = 0; i < n; ++i) g.on_decode(out[i], g.context);
  set_state(State::ready);
}


// One log line per raw frame that reached the soft decoder (host tool js8-wav-front prints the same fields), plus one per verified message.
void log_js8_frames(const char* tag) {
  for (size_t i = 0; i < g.js8->raw_count(); ++i) {
    const auto* r = g.js8->raw(i);
    if (!r->attempted) continue;
    std::printf("%s_FRAME hz=%.2f dt_ms=%d sync_hits=%u sync=%.3f margin=%.3f initial_syndrome=%u bp_iter=%u osd_order=%u corrections=%u final_syndrome=%u crc=%s",
                tag, static_cast<double>(r->audio_hz), static_cast<int>(r->dt_ms), static_cast<unsigned>(r->sync_hits), static_cast<double>(r->sync_score),
                static_cast<double>(r->mean_margin), static_cast<unsigned>(r->initial_syndrome), static_cast<unsigned>(r->bp_iterations),
                static_cast<unsigned>(r->osd_order), static_cast<unsigned>(r->hard_corrections), static_cast<unsigned>(r->final_syndrome), r->crc_valid ? "ok" : "fail");
    if (r->crc_valid) std::printf(" kind=%u payload=%s", static_cast<unsigned>(r->frame_kind), r->payload);
    if (r->rendered) std::printf(" text=%s", r->text);
    std::printf("%s", "\n");
  }
}

void run_js8_injection() {
  if (g.js8 == nullptr) {
    std::printf("ORC_JS8_INJECT_ERROR no_backend\n");
    return;
  }
  g.js8->begin_slot(1791446400000ull);
  size_t pos = 0;
  while (pos < g.inject_count) {
    const size_t take = std::min<size_t>(4096, g.inject_count - pos);
    g.js8->offer_audio(g.inject + pos, take);
    pos += take;
  }
  orcsdr::ft8::Decode jout[8];
  const size_t n = g.js8->finish_slot(jout, sizeof(jout) / sizeof(jout[0]));
  const auto& st = g.js8->stats();
  std::printf("ORC_JS8_INJECT_RESULT submode=normal samples=%u decodes=%u raw_frames=%u candidates=%u strong=%u best_sync=%.2f total_ms=%u spectral=%u search=%u demod=%u rows=%u\n",
              static_cast<unsigned>(g.inject_count), static_cast<unsigned>(n), static_cast<unsigned>(st.raw_frames), static_cast<unsigned>(st.candidates),
              static_cast<unsigned>(st.strong_candidates), static_cast<double>(st.best_sync_score), static_cast<unsigned>(st.total_ms),
              static_cast<unsigned>(st.spectral_ms), static_cast<unsigned>(st.search_ms), static_cast<unsigned>(st.demod_ms),
              static_cast<unsigned>(st.grid_rows));
  for (size_t i = 0; i < g.js8->raw_count(); ++i) {
    const auto* r = g.js8->raw(i);
    std::printf("ORC_JS8_INJECT_RAW hz=%.1f dt_ms=%d sync=%.2f hits=%u margin=%.2f\n", static_cast<double>(r->audio_hz), static_cast<int>(r->dt_ms),
                static_cast<double>(r->sync_score), static_cast<unsigned>(r->sync_hits), static_cast<double>(r->mean_margin));
  }
  log_js8_frames("ORC_JS8_INJECT");
  std::printf("ORC_JS8_INJECT_DONE\n");
}

void run_injection() {
  if (is_js8(g.inject_mode)) {
    run_js8_injection();
    return;
  }
  const orcsdr::ft8::DigitalMode keep = g.mode;
  g.backend->set_mode(ftx_mode(g.inject_mode));
  g.backend->begin_slot(1791446400000ull);
  size_t pos = 0;
  while (pos < g.inject_count) {
    const size_t take = std::min<size_t>(4096, g.inject_count - pos);
    g.backend->offer_audio(g.inject + pos, take);
    pos += take;
  }
  orcsdr::ft8::Decode out[kDecodeCapacity];
  const size_t n = g.backend->finish_slot(out, kDecodeCapacity);
  const auto& st = g.backend->stats();
  std::printf("ORC_FT8_INJECT_RESULT mode=%d samples=%u decodes=%u total_ms=%u spectral=%u search=%u refine=%u gates=%u coarse=%u strong=%u\n",
              static_cast<int>(g.inject_mode), static_cast<unsigned>(g.inject_count), static_cast<unsigned>(n), static_cast<unsigned>(st.total_ms),
              static_cast<unsigned>(st.spectral_ms), static_cast<unsigned>(st.search_ms), static_cast<unsigned>(st.refine_ms),
              static_cast<unsigned>(st.gate_ms), static_cast<unsigned>(st.coarse_candidates), static_cast<unsigned>(st.strong_candidates));
  for (size_t i = 0; i < n; ++i)
    std::printf("ORC_FT8_INJECT_DECODE hz=%u dt_ms=%d msg=[%s]\n", static_cast<unsigned>(out[i].audio_hz), static_cast<int>(out[i].dt_ms), out[i].message);
  std::printf("ORC_FT8_INJECT_DONE\n");
  g.backend->set_mode(ftx_mode(keep));
}

void decoder_task(void*) {
  uint64_t last_slot = UINT64_MAX;
  g.task_alive.store(true, std::memory_order_release);
  while (g.active.load(std::memory_order_acquire)) {
    vTaskDelay(pdMS_TO_TICKS(100));
    if (g.dial_offset_fn != nullptr) g.dial_offset_hz.store(g.dial_offset_fn(), std::memory_order_release);
    if (!g.headless.load(std::memory_order_acquire) &&
        esp_timer_get_time() - g.last_touch_us.load(std::memory_order_acquire) > 8000000) {
      g.active.store(false, std::memory_order_release);
      g.tap_ready = false;
      set_state(State::stopped);
      std::printf("ORC_FT8_RT stopped (screen left)\n");
      break;
    }
    if (g.inject_pending.exchange(false, std::memory_order_acq_rel)) run_injection();
    if (g.config_dirty.exchange(false, std::memory_order_acq_rel)) {
      orcsdr::ftx::native::Config c = g.backend->config();
      c.candidate_k = g.cfg_k;
      c.gate = g.cfg_gate;
      c.fine_rows = g.cfg_fine_rows;
      c.deadline_ms = g.cfg_deadline_ms;
      g.backend->set_config(c);
    }
    if (g.mode_changed.exchange(false, std::memory_order_acq_rel)) {
      if (is_js8(g.mode)) {
        if (!js8_attach()) std::printf("ORC_JS8_RT attach_failed\n");
        else g.js8_stats.attached = true;
      } else {
        g.backend->set_mode(ftx_mode(g.mode));
      }
      g.js8_stats.active = is_js8(g.mode);
      last_slot = UINT64_MAX;
    }
    if (g.clock_valid != nullptr && !g.clock_valid()) {
      set_state(State::waiting_clock);
      continue;
    }
    if (!g.tap_ready) {
      set_state(State::waiting_signal);
      continue;
    }
    if (static_cast<State>(g.state.load(std::memory_order_acquire)) == State::waiting_clock ||
        static_cast<State>(g.state.load(std::memory_order_acquire)) == State::waiting_signal)
      set_state(State::listening);

    const uint32_t slot_ms = slot_ms_for(g.mode);
    const uint64_t t_before = g.total.load(std::memory_order_acquire);
    const uint64_t now_ms = wall_ms();
    const uint64_t total_now = g.total.load(std::memory_order_acquire);
    (void)t_before;
    const uint64_t current = now_ms / slot_ms;
    if (last_slot == UINT64_MAX) {
      last_slot = current;
      continue;
    }
    if (current > last_slot && (now_ms % slot_ms) >= kStartDelayMs) {
      const uint64_t finished = current - 1;
      last_slot = current;
      decode_slot(finished * slot_ms, slot_ms, now_ms, total_now);
    }
  }
  g.task_alive.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}


void waterfall_task(void*) {
  g.wf_alive.store(true, std::memory_order_release);
  auto* plan = static_cast<orcsdr::ftx::spectral_fft::Plan*>(psram_alloc(sizeof(orcsdr::ftx::spectral_fft::Plan)));
  auto* scratch = static_cast<orcsdr::ftx::spectral_fft::Scratch*>(psram_alloc(sizeof(orcsdr::ftx::spectral_fft::Scratch)));
  auto* window = static_cast<int16_t*>(psram_alloc(1920 * sizeof(int16_t)));
  auto* power = static_cast<float*>(psram_alloc(kWaterfallBins * sizeof(float)));
  auto* sorted = static_cast<float*>(psram_alloc(kWaterfallBins * sizeof(float)));
  bool ok = plan && scratch && window && power && sorted;
  if (ok) {
    new (plan) orcsdr::ftx::spectral_fft::Plan();
    new (scratch) orcsdr::ftx::spectral_fft::Scratch();
    ok = orcsdr::ftx::spectral_fft::make_plan(plan, 1920);
  }
  uint64_t last_total = 0;
  uint32_t wf_ms_sum = 0, wf_rows_timed = 0;
  while (ok && g.active.load(std::memory_order_acquire)) {
    vTaskDelay(pdMS_TO_TICKS(60));   // short period + overlapping 160 ms windows give a smooth scroll
    if (!g.tap_ready) continue;
    const uint64_t total = g.total.load(std::memory_order_acquire);
    if (total < 1920) continue;
    if (last_total == 0 || total - last_total > 840u * 6u) last_total = total - 840u;   // (re)start: one row now, no long catch-up
    // One row per 70 ms of audio. The tap hands over audio in bursts about every 120 ms, so a burst yields two rows
    // (windows ending 70 ms apart) and the scroll can run at a steady pace instead of in steps.
    while (total - last_total >= 840u && g.active.load(std::memory_order_acquire)) {
    last_total += 840u;
    const int64_t wf_t0 = esp_timer_get_time();
    for (size_t i = 0; i < 1920; ++i) window[i] = g.ring[(last_total - 1920 + i) % kRingSamples];
    orcsdr::ftx::spectral_fft::power_bins(*plan, scratch, window, 1920, 32, kWaterfallBins, power);
    std::memcpy(sorted, power, kWaterfallBins * sizeof(float));
    std::nth_element(sorted, sorted + kWaterfallBins / 2, sorted + kWaterfallBins);
    const float floor_db = 10.0f * std::log10(sorted[kWaterfallBins / 2] + 1.0e-9f);
    uint8_t* row = g.wf + (g.wf_sequence.load(std::memory_order_relaxed) % kWaterfallRows) * kWaterfallBins;
    for (size_t b = 0; b < kWaterfallBins; ++b) {
      const float v = (10.0f * std::log10(power[b] + 1.0e-9f) - floor_db - 3.0f) * 8.0f;   // noise stays dark; +10 dB reads about 56, +30 dB about 216
      row[b] = static_cast<uint8_t>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
    }
    g.wf_sequence.fetch_add(1, std::memory_order_release);
    wf_ms_sum += static_cast<uint32_t>((esp_timer_get_time() - wf_t0) / 1000);
    if (++wf_rows_timed == 100) {   // row cost, for tuning the scroll rate
      std::printf("ORC_FT8_RT wf_row_ms_avg=%u\n", static_cast<unsigned>(wf_ms_sum / 100));
      wf_ms_sum = 0;
      wf_rows_timed = 0;
    }
    }   // while rows pending
  }
  if (plan) psram_free(plan);
  if (scratch) psram_free(scratch);
  if (window) psram_free(window);
  if (power) psram_free(power);
  if (sorted) psram_free(sorted);
  g.wf_alive.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}

}  // namespace

WaterfallView waterfall() {
  WaterfallView v;
  v.data = g.wf;
  v.sequence = g.wf_sequence.load(std::memory_order_acquire);
  return v;
}

bool active() { return g.active.load(std::memory_order_acquire); }

static void mem_mark(const char* stage) {   // internal DMA-capable heap at each start step (the SD card needs it)
  std::printf("ORC_FT8_RT mem stage=%s dma_free=%u dma_largest=%u\n", stage,
              static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
              static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)));
}

bool start(orcsdr::ft8::DigitalMode mode, DecodeCallback on_decode, void* context, ClockValidFn clock_valid, DialOffsetFn dial_offset) {
  if (g.active.load(std::memory_order_acquire)) return true;
  mem_mark("entry");
  if (g.task_alive.load(std::memory_order_acquire) || g.wf_alive.load(std::memory_order_acquire)) return false;  // previous tasks still exiting
  if (g.wf == nullptr) {
    g.wf = static_cast<uint8_t*>(psram_alloc(kWaterfallRows * kWaterfallBins));
    if (g.wf == nullptr) return false;
  }
  if (g.ring == nullptr) {
    g.ring = static_cast<int16_t*>(psram_alloc(kRingSamples * sizeof(int16_t)));
    if (g.ring == nullptr) return false;
  }
  if (g.snapshot == nullptr) {
    g.snapshot = static_cast<int16_t*>(psram_alloc((12000u * 16u) * sizeof(int16_t)));
    if (g.snapshot == nullptr) return false;
  }
  if (g.backend == nullptr) {
    void* mem = psram_alloc(sizeof(orcsdr::ftx::native::Backend));
    if (mem == nullptr) return false;
    g.backend = new (mem) orcsdr::ftx::native::Backend();
  }
  mem_mark("buffers");
  orcsdr::ftx::native::Config config;
  config.candidate_k = g.cfg_k;
  config.gate = g.cfg_gate;
  config.fine_rows = g.cfg_fine_rows;
  config.deadline_ms = g.cfg_deadline_ms;       // finish inside the slot that follows
  config.now_us = now_us_fn;
  orcsdr::ftx::native::Memory memory;
  memory.alloc = psram_alloc;
  memory.release = psram_free;
  if (!g.backend->begin(12000, ftx_mode(mode), config, memory)) {
    std::printf("ORC_FT8_RT start_failed backend\n");
    return false;
  }
  if (is_js8(mode) && (!js8_supported(mode) || !js8_attach())) {
    std::printf("ORC_FT8_RT start_failed js8\n");
    return false;
  }
  g.mode = mode;
  g.on_decode = on_decode;
  g.context = context;
  g.clock_valid = clock_valid;
  g.dial_offset_fn = dial_offset;
  g.dial_offset_hz.store(dial_offset != nullptr ? dial_offset() : 0.0f, std::memory_order_release);
  g.tap_ready = false;
  g.input_rate = 0;
  g.total.store(0, std::memory_order_release);
  g.cont_ms.store(wall_ms(), std::memory_order_release);
  g.cont_total.store(0, std::memory_order_release);
  g.status = Status{};
  g.max_block_us.store(0);
  g.blocks_us_sum.store(0);
  g.blocks_us_n.store(0);
  g.tap_blocks.store(0);
  g.reset_pending.store(true, std::memory_order_release);
  g.last_touch_us.store(esp_timer_get_time(), std::memory_order_release);
  set_state(State::waiting_signal);
  g.active.store(true, std::memory_order_release);
  if (xTaskCreatePinnedToCoreWithCaps(decoder_task, "ft8_native", 20480, nullptr, 1, &g.task, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    g.active.store(false, std::memory_order_release);
    set_state(State::error);
    std::printf("ORC_FT8_RT start_failed task\n");
    return false;
  }
  mem_mark("backend");
  (void)xTaskCreatePinnedToCoreWithCaps(waterfall_task, "ft8_wf", 8192, nullptr, 1, &g.wf_task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  mem_mark("tasks");
  std::printf("ORC_FT8_RT started mode=%d\n", static_cast<int>(mode));
  return true;
}

void stop() {
  g.active.store(false, std::memory_order_release);
  g.tap_ready = false;
  set_state(State::stopped);
}

bool set_mode(orcsdr::ft8::DigitalMode mode) {
  const bool ftx = mode == orcsdr::ft8::DigitalMode::ft8 || mode == orcsdr::ft8::DigitalMode::ft4;
  if (!ftx && !js8_supported(mode)) return false;   // JS8 Fast/40/Slow/60 stay refused; never mapped to Normal
  g.mode = mode;
  g.mode_changed.store(true, std::memory_order_release);
  g.reset_pending.store(true, std::memory_order_release);
  return true;
}

void touch() { g.last_touch_us.store(esp_timer_get_time(), std::memory_order_release); }

void note_discontinuity() { g.reset_pending.store(true, std::memory_order_release); }

void offer_iq(const uint8_t* iq, size_t bytes, uint32_t sample_rate_hz) {
  if (!g.active.load(std::memory_order_acquire) || g.ring == nullptr) return;
  if (!g.headless.load(std::memory_order_acquire) &&
      esp_timer_get_time() - g.last_touch_us.load(std::memory_order_acquire) > 6000000)
    return;   // screen is gone
  if (sample_rate_hz != g.input_rate) {
    g.tap_ready = tap_ns::begin(&g.tap, sample_rate_hz);
    g.input_rate = sample_rate_hz;
    g.reset_pending.store(false, std::memory_order_release);
    mark_discontinuity();
  } else if (g.reset_pending.exchange(false, std::memory_order_acq_rel)) {
    tap_ns::reset(&g.tap);
    mark_discontinuity();
  }
  if (!g.tap_ready) return;
  {
    // Follow the tuner: when the dial's position in the baseband moves, shift the passband (a retune is also a discontinuity).
    const float wanted = g.dial_offset_hz.load(std::memory_order_acquire);
    const float have = g.tap.dial_offset_hz;
    if (wanted != have) {
      tap_ns::set_dial_offset(&g.tap, wanted);
      if (std::fabs(wanted - have) > 200.0f) {
        tap_ns::reset(&g.tap);
        mark_discontinuity();
      }
    }
  }

  const int64_t t0 = esp_timer_get_time();
  static int16_t out[512];
  uint64_t total = g.total.load(std::memory_order_relaxed);
  size_t offset = 0;
  while (offset < bytes) {
    const size_t take = (bytes - offset) > 8192u ? 8192u : (bytes - offset);
    const size_t n = tap_ns::process_cu8(&g.tap, iq + offset, take, out, sizeof(out) / sizeof(out[0]));
    for (size_t i = 0; i < n; ++i) g.ring[(total + i) % kRingSamples] = out[i];
    total += n;
    offset += take;
  }
  g.total.store(total, std::memory_order_release);

  const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
  g.tap_blocks.fetch_add(1, std::memory_order_relaxed);
  g.blocks_us_sum.fetch_add(us, std::memory_order_relaxed);
  g.blocks_us_n.fetch_add(1, std::memory_order_relaxed);
  uint32_t prev = g.max_block_us.load(std::memory_order_relaxed);
  while (us > prev && !g.max_block_us.compare_exchange_weak(prev, us, std::memory_order_relaxed)) {
  }
}


bool inject_begin(size_t samples) {
  if (!g.active.load(std::memory_order_acquire) || samples == 0 || samples > 12000u * 16u) return false;
  if (g.inject == nullptr) {
    g.inject = static_cast<int16_t*>(psram_alloc(12000u * 16u * sizeof(int16_t)));
    if (g.inject == nullptr) return false;
    g.inject_capacity = 12000u * 16u;
  }
  std::memset(g.inject, 0, g.inject_capacity * sizeof(int16_t));
  g.inject_count = samples;
  g.inject_written = 0;
  return true;
}

bool inject_write(size_t offset, const int16_t* data, size_t count) {
  if (g.inject == nullptr || data == nullptr || offset + count > g.inject_count) return false;
  std::memcpy(g.inject + offset, data, count * sizeof(int16_t));
  g.inject_written += count;
  return true;
}

size_t inject_written() { return g.inject_written; }

uint32_t inject_crc32() {
  uint32_t crc = 0xFFFFFFFFu;
  const uint8_t* b = reinterpret_cast<const uint8_t*>(g.inject);
  for (size_t i = 0; g.inject != nullptr && i < g.inject_count * 2; ++i) {
    crc ^= b[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

bool inject_run(orcsdr::ft8::DigitalMode mode) {
  if (g.inject == nullptr || g.inject_count == 0 || !g.active.load(std::memory_order_acquire)) return false;
  if (mode != orcsdr::ft8::DigitalMode::ft8 && mode != orcsdr::ft8::DigitalMode::ft4 && !js8_supported(mode)) return false;
  if (is_js8(mode) && !js8_attach()) return false;
  g.inject_mode = mode;
  g.inject_pending.store(true, std::memory_order_release);
  return true;
}

void set_headless(bool headless) {
  g.headless.store(headless, std::memory_order_release);
  if (headless) touch();
}

bool set_config(uint16_t k, uint16_t gate, uint8_t fine_rows, uint32_t deadline_ms) {
  if ((fine_rows != 4 && fine_rows != 8) || k < 1 || k > orcsdr::ftx::native::Backend::kMaxCandidateK || gate < 1 || gate > orcsdr::ftx::native::Backend::kMaxCandidateK)
    return false;
  g.cfg_k = k;
  g.cfg_gate = gate;
  g.cfg_fine_rows = fine_rows;
  g.cfg_deadline_ms = deadline_ms;
  g.config_dirty.store(true, std::memory_order_release);
  return true;
}

bool last_slot_audio(const int16_t** samples, size_t* count, uint64_t* slot_epoch_ms) {
  if (g.snapshot == nullptr || !g.have_slot || samples == nullptr || count == nullptr || slot_epoch_ms == nullptr) return false;
  g.snapshot_hold.store(true, std::memory_order_release);   // keeps the copy still until release_slot_audio()
  *samples = g.snapshot;
  *count = g.snapshot_count;
  *slot_epoch_ms = g.snapshot_slot_ms;
  return g.snapshot_count > 0;
}

void release_slot_audio() { g.snapshot_hold.store(false, std::memory_order_release); }

Status status() {
  Status s = g.status;
  s.mode = g.mode;
  s.headless = g.headless.load(std::memory_order_acquire);
  s.cfg_k = g.cfg_k;
  s.dial_offset_hz = g.tap.dial_offset_hz;
  s.cfg_gate = g.cfg_gate;
  s.cfg_fine_rows = g.cfg_fine_rows;
  s.cfg_deadline_ms = g.cfg_deadline_ms;
  s.state = static_cast<State>(g.state.load(std::memory_order_acquire));
  s.tap_running = g.tap_ready && g.active.load(std::memory_order_acquire);
  s.input_rate_hz = g.input_rate;
  s.tap_blocks = g.tap_blocks.load(std::memory_order_relaxed);
  s.ring_samples = g.total.load(std::memory_order_acquire);
  s.max_block_us = g.max_block_us.load(std::memory_order_relaxed);
  const uint32_t n = g.blocks_us_n.load(std::memory_order_relaxed);
  s.avg_block_us = n != 0 ? g.blocks_us_sum.load(std::memory_order_relaxed) / n : 0;
  return s;
}

Js8Stats js8_stats() {
  Js8Stats copy = g.js8_stats;
  copy.attached = g.js8 != nullptr;
  copy.active = g.active.load(std::memory_order_acquire) && is_js8(g.mode);
  return copy;
}

size_t js8_raw(Js8Raw* out, size_t capacity) {
  if (out == nullptr) return 0;
  const size_t count = std::min<size_t>(g.js8_raw_count.load(std::memory_order_acquire), capacity);
  for (size_t i = 0; i < count; ++i) out[i] = g.js8_raw[i];
  return count;
}

}  // namespace orcsdr::ft8_runtime

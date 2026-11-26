#include "dv_voice_service.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include <chrono>
#endif

namespace orcsdr::dv_voice_service {
namespace {
using dv_voice::Engine;

Hooks g_hooks{};
Engine* g_engine = nullptr;
std::atomic<bool> g_running{false}, g_task_run{false}, g_task_on{false};
std::atomic<bool> g_enabled{true};
std::atomic<uint8_t> g_listen_req{0xff};     // 0xff = no request
std::atomic<bool> g_reset_req{false};
std::atomic<uint32_t> g_tone_ms{0};
std::atomic<uint32_t> g_gain_req{0xffffffffu};      // requested playback gain x100; all-ones = none
std::atomic<uint32_t> g_gain_x100{600};              // current playback gain x100 (default 6.00: the vocoder output is quiet; peaks of a real call were -20 dBFS raw)
std::atomic<uint32_t> g_buffer_req{0xffffffffu};   // requested playout hold (ms); all-ones = none
std::atomic<uint32_t> g_nfm_blocked{0};
std::atomic<uint8_t> g_save_req{0xff};          // 0/1 = requested, 0xff = none
std::atomic<bool> g_save_on{false};
dv_speech_rec::Recorder* g_rec = nullptr;
uint32_t g_last_pull_ms = 0, g_pumps = 0, g_max_pump_us = 0, g_last_anchor_ms = 0, g_last_qualified_ms = 0;
uint32_t g_prev_anchors = 0, g_prev_qualified = 0;
bool g_exclusive = false;

// Seqlock: one writer (the pump), any number of readers.
std::atomic<uint32_t> g_seq{0};
// Large state lives in PSRAM (allocated by start()): the Tab5 internal RAM boot pool has no room for statics.
Published* g_pub = nullptr;
uint32_t g_revision = 0;
Published* g_last_sig = nullptr;

uint32_t now_ms() { return g_hooks.now_ms ? g_hooks.now_ms() : 0u; }
uint32_t micros() {
#if defined(ESP_PLATFORM)
  return static_cast<uint32_t>(esp_timer_get_time());
#else
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
void* alloc(size_t bytes) {
#if defined(ESP_PLATFORM)
  return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::calloc(1, bytes);
#endif
}
void release(void* p) {
#if defined(ESP_PLATFORM)
  heap_caps_free(p);
#else
  std::free(p);
#endif
}

void publish(uint32_t now) {
  Published p{};
  p.engine = g_engine->snapshot(now);
  p.running = true;
  p.allowed = g_hooks.audio_allowed ? g_hooks.audio_allowed() : true;
  p.path_ready = g_hooks.audio_path_ready ? g_hooks.audio_path_ready() : true;
  p.nfm_blocked_blocks = g_nfm_blocked.load(std::memory_order_relaxed);
  p.pumps = g_pumps;
  p.max_pump_us = g_max_pump_us;
  p.queue_pending = dv_radio::voice_status().queue_pending;
#if defined(ESP_PLATFORM)
  p.stack_hwm_bytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)) * sizeof(StackType_t);
#endif
  // Voice sync on the air but nothing qualified (yet): ACQUIRING VOICE.
  const auto vs = dv_radio::voice_status();
  if (vs.anchors != g_prev_anchors) { g_prev_anchors = vs.anchors; g_last_anchor_ms = now; }
  if (vs.qualified != g_prev_qualified) { g_prev_qualified = vs.qualified; g_last_qualified_ms = now; }
  using Ph = dv_voice::Phase;
  if (p.engine.phase == Ph::listening && g_last_anchor_ms && now - g_last_anchor_ms < 1500 &&
      (g_last_qualified_ms == 0 || g_last_anchor_ms > g_last_qualified_ms))
    p.engine.phase = Ph::acquiring;
  if (!p.path_ready && p.engine.phase != Ph::protected_voice && p.engine.phase != Ph::muted) p.engine.phase = Ph::unavailable;
  if (!p.allowed && p.engine.phase != Ph::protected_voice) p.engine.phase = Ph::muted;
  // Revision moves only when something the screen shows changes (meter and counters are coarse on purpose).
  p.revision = g_revision;
  const auto& a = p.engine; const auto& b = g_last_sig->engine;
  const bool changed = a.phase != b.phase || a.listen != b.listen || a.active_slot != b.active_slot ||
      a.stats.frames_speech != b.stats.frames_speech || a.stats.frames_silence != b.stats.frames_silence ||
      a.stats.frames_error != b.stats.frames_error || a.stats.underruns != b.stats.underruns ||
      a.privacy[1] != b.privacy[1] || a.privacy[2] != b.privacy[2] || a.source_id != b.source_id ||
      a.destination_id != b.destination_id || p.path_ready != g_last_sig->path_ready || p.allowed != g_last_sig->allowed ||
      a.stats.buffered_ms / 50 != b.stats.buffered_ms / 50 || a.stats.rms_last / 1024 != b.stats.rms_last / 1024;
  if (changed) { ++g_revision; p.revision = g_revision; *g_last_sig = p; }
  g_seq.fetch_add(1, std::memory_order_acq_rel);
  *g_pub = p;
  g_seq.fetch_add(1, std::memory_order_release);
}

void speech_sink(uint8_t slot, uint32_t serial, uint32_t src, uint32_t dst, const int16_t* pcm, bool silent, uint32_t now, void*) {
  if (g_rec) g_rec->frame(slot, serial, src, dst, pcm, silent, now);
}

void task_main(void*) {
  while (g_task_run.load(std::memory_order_acquire)) {
    pump();
    if (g_hooks.sleep_ms) g_hooks.sleep_ms(5);
  }
}
}  // namespace

void install(const Hooks& hooks) { g_hooks = hooks; }
bool running() { return g_running.load(std::memory_order_acquire); }
bool enabled() { return g_enabled.load(std::memory_order_relaxed); }
void set_enabled(bool on) { g_enabled.store(on, std::memory_order_relaxed); }
void set_listen(dv_voice::Listen l) { g_listen_req.store(static_cast<uint8_t>(l), std::memory_order_release); }
void set_gain(float g) { const uint32_t x = static_cast<uint32_t>((g < 0.25f ? 0.25f : g > 16.0f ? 16.0f : g) * 100.0f + 0.5f); g_gain_x100.store(x, std::memory_order_relaxed); g_gain_req.store(x, std::memory_order_release); }
float gain() { return static_cast<float>(g_gain_x100.load(std::memory_order_relaxed)) / 100.0f; }
void reset() { g_reset_req.store(true, std::memory_order_release); }
void set_buffer_ms(uint32_t ms) { g_buffer_req.store(ms, std::memory_order_release); }
bool play_tone(uint32_t ms) {
  if (!running() || ms == 0) return false;
  g_tone_ms.store(ms > 1500 ? 1500 : ms, std::memory_order_release);
  return true;
}
void set_save_speech(bool on) { g_save_on.store(on, std::memory_order_relaxed); g_save_req.store(on ? 1 : 0, std::memory_order_release); }
bool save_speech() { return g_save_on.load(std::memory_order_relaxed); }
bool speech_ready(const int16_t** pcm, size_t* samples, char* name, size_t name_size, uint8_t header[dv_speech_rec::kHeaderBytes]) {
  return g_rec && g_rec->ready(pcm, samples, name, name_size, header);
}
void speech_release(bool saved) { if (g_rec) g_rec->release(saved); }
dv_speech_rec::Stats speech_stats() { return g_rec ? g_rec->stats() : dv_speech_rec::Stats{}; }
void note_nfm_blocked() { g_nfm_blocked.fetch_add(1, std::memory_order_relaxed); }
uint32_t nfm_blocked_blocks() { return g_nfm_blocked.load(std::memory_order_relaxed); }

bool start() {
  if (running()) return true;
  void* mem = alloc(sizeof(Engine));
  if (!mem) return false;
  g_engine = new (mem) Engine();
  void* pub = alloc(sizeof(Published) * 2);
  if (!pub || !g_engine->begin()) {
    if (pub) release(pub);
    g_engine->~Engine();
    release(g_engine);
    g_engine = nullptr;
    return false;
  }
  g_engine->set_gain(static_cast<float>(g_gain_x100.load(std::memory_order_relaxed)) / 100.0f);
  g_engine->set_enabled(g_enabled.load());
  g_rec = new (alloc(sizeof(dv_speech_rec::Recorder))) dv_speech_rec::Recorder();
  g_engine->set_speech_sink(speech_sink, nullptr);
  if (g_save_on.load() && g_rec->begin()) g_rec->set_enabled(true);
  g_last_pull_ms = now_ms();
  g_pumps = g_max_pump_us = 0;
  g_prev_anchors = g_prev_qualified = 0;
  g_last_anchor_ms = g_last_qualified_ms = 0;
  g_pub = new (pub) Published();
  g_last_sig = new (static_cast<Published*>(pub) + 1) Published();
  g_listen_req.store(0xff);
  g_reset_req.store(false);
  g_tone_ms.store(0);
  dv_radio::set_voice_queue_enabled(true);
  if (g_hooks.set_exclusive) { g_hooks.set_exclusive(true); g_exclusive = true; }
  g_running.store(true, std::memory_order_release);
  if (g_hooks.spawn) {
    g_task_run.store(true, std::memory_order_release);
    if (g_hooks.spawn(task_main, nullptr)) g_task_on.store(true, std::memory_order_release);
    else g_task_run.store(false, std::memory_order_release);
  }
  return true;
}

void stop() {
  if (!running()) return;
  g_running.store(false, std::memory_order_release);
  if (g_task_on.load()) {
    g_task_run.store(false, std::memory_order_release);
    if (g_hooks.join) g_hooks.join();
    g_task_on.store(false);
  }
  dv_radio::set_voice_queue_enabled(false);
  if (g_exclusive && g_hooks.set_exclusive) g_hooks.set_exclusive(false);
  g_exclusive = false;
  if (g_rec) { g_rec->flush(); }
  if (g_engine) {
    g_engine->~Engine();
    release(g_engine);
    g_engine = nullptr;
  }
  if (g_rec) { g_rec->~Recorder(); release(g_rec); g_rec = nullptr; }
  if (g_pub) { release(g_pub); g_pub = nullptr; g_last_sig = nullptr; }   // g_last_sig shares g_pub's block
}

void pump() {
  if (!running() || !g_engine) return;
  const uint32_t t0 = micros();
  const uint32_t now = now_ms();
  Engine& e = *g_engine;
  const uint8_t lr = g_listen_req.exchange(0xff, std::memory_order_acq_rel);
  if (lr <= 3) e.set_listen(static_cast<dv_voice::Listen>(lr));
  if (g_reset_req.exchange(false, std::memory_order_acq_rel)) e.reset_stream();
  const uint32_t gr = g_gain_req.exchange(0xffffffffu, std::memory_order_acq_rel);
  if (gr != 0xffffffffu) e.set_gain(static_cast<float>(gr) / 100.0f);
  const uint32_t br = g_buffer_req.exchange(0xffffffffu, std::memory_order_acq_rel);
  if (br != 0xffffffffu) e.set_target_buffer_ms(br);
  e.set_enabled(g_enabled.load(std::memory_order_relaxed));
  const uint8_t sv = g_save_req.exchange(0xff, std::memory_order_acq_rel);
  if (sv != 0xff && g_rec) {
    if (sv && !g_rec->begin()) g_save_on.store(false);     // no memory: stays off
    else g_rec->set_enabled(sv != 0);
  }
  if (g_rec) {
    if (g_hooks.utc_epoch) g_rec->set_clock(g_hooks.utc_epoch());
    g_rec->service(now);
  }
  const bool allowed = g_hooks.audio_allowed ? g_hooks.audio_allowed() : true;
  e.set_muted(!allowed);
  const uint32_t tone_ms = g_tone_ms.exchange(0, std::memory_order_acq_rel);
  if (tone_ms) {
    int16_t frame[160];
    for (uint32_t f = 0; f < tone_ms / 20; ++f) {
      for (int i = 0; i < 160; ++i)
        frame[i] = static_cast<int16_t>(9000.0 * std::sin(2.0 * 3.14159265358979 * 1000.0 * (f * 160 + i) / 8000.0));
      if (!e.inject_test_pcm(frame, 160, now)) break;
    }
  }
  dv_radio::VoiceMsg m;
  for (int n = 0; n < 8 && dv_radio::pop_voice(&m); ++n) e.handle(m, now, true);
  (void)e.service(now, 3);                    // about 20 ms of codec work per step: the speaker keeps being fed in between
  // Real-time playout: ask for exactly the samples that elapsed since the last step (no catch-up burst after a stall).
  uint32_t dt = now - g_last_pull_ms;
  g_last_pull_ms = now;
  if (dt > 100) dt = 100;
  size_t due = static_cast<size_t>(dt) * 48u;
  const bool ready = g_hooks.audio_path_ready ? g_hooks.audio_path_ready() : true;
  while (due > 0) {
    int16_t buf[960];
    const size_t want = due < 960 ? due : 960;
    const size_t got = e.pull(buf, want, now);
    if (got && allowed && ready && g_hooks.audio_out) g_hooks.audio_out(buf, got);
    if (got < want) break;
    due -= got;
  }
  ++g_pumps;
  const uint32_t dtu = micros() - t0;
  if (dtu > g_max_pump_us) g_max_pump_us = dtu;
  publish(now);
}

Published snapshot() {
  Published out{};
  for (int i = 0; i < 4; ++i) {
    const uint32_t a = g_seq.load(std::memory_order_acquire);
    if (a & 1u) continue;
    if (g_pub) out = *g_pub;
    if (g_seq.load(std::memory_order_acquire) == a) break;
  }
  out.running = running();
  return out;
}

void format_status(char* out, size_t size) {
  auto U = [](uint32_t v) { return static_cast<unsigned>(v); };
  const Published p = snapshot();
  const auto& s = p.engine.stats;
  const auto vs = dv_radio::voice_status();
  std::snprintf(out, size,
      "RTL_DV_AUDIO buffer_ms=%u running=%d enabled=%d allowed=%d path=%d phase=%s listen=%s slot=%u priv1=%u priv2=%u groups=%llu frames=%llu speech=%llu "
      "silence=%llu errors=%llu concealed=%llu corrected=%llu uncorrectable=%llu lost_sf=%llu underruns=%llu overruns=%llu flushes=%llu "
      "synth_us=%u/%u/%u buffered_ms=%u peak_ms=%u latency_ms=%u gap_ms=%u/%u rms=%d peak=%d clipped=%llu test_samples=%llu nfm_blocked=%u "
      "q_pending=%u q_drops=%u qual=%u prot=%u unq=%u calls=%u pumps=%u pump_max_us=%u stack_hwm=%u rec=%d rec_saved=%u rec_discarded=%u rec_busy=%u",
      U(p.engine.stats.target_buffer_ms), p.running ? 1 : 0, enabled() ? 1 : 0, p.allowed ? 1 : 0, p.path_ready ? 1 : 0, dv_voice::phase_name(p.engine.phase),
      dv_voice::listen_name(p.engine.listen), static_cast<unsigned>(p.engine.active_slot), static_cast<unsigned>(p.engine.privacy[1]),
      static_cast<unsigned>(p.engine.privacy[2]), (unsigned long long)s.groups, (unsigned long long)s.frames,
      (unsigned long long)s.frames_speech, (unsigned long long)s.frames_silence, (unsigned long long)s.frames_error,
      (unsigned long long)s.frames_concealed, (unsigned long long)s.corrected_bits, (unsigned long long)s.frames_error,
      (unsigned long long)s.lost_superframes, (unsigned long long)s.underruns, (unsigned long long)s.overruns,
      (unsigned long long)s.flushes, U(s.synth_us_last), U(s.synth_us_max), U(s.synth_us_avg), U(s.buffered_ms), U(s.buffered_peak_ms),
      U(s.latency_ms_last), U(s.gap_min_ms), U(s.gap_max_ms), static_cast<int>(s.rms_last), static_cast<int>(s.peak_last), (unsigned long long)s.clipped_samples,
      (unsigned long long)s.test_samples, U(p.nfm_blocked_blocks), U(p.queue_pending), U(vs.queue_drops), U(vs.qualified),
      U(vs.protected_groups), U(vs.unqualified), U(vs.calls_started), U(p.pumps), U(p.max_pump_us), U(p.stack_hwm_bytes), save_speech() ? 1 : 0,
      U(speech_stats().calls_saved), U(speech_stats().calls_discarded), U(speech_stats().calls_dropped_busy));
}

void format_support(char* out, size_t size) {
  if (!out || size == 0) return;
  out[0] = 0;
  if (!running()) return;
  const Published p = snapshot();
  const auto& s = p.engine.stats;
  const auto vs = dv_radio::voice_status();
  std::snprintf(out, size,
      "ORC_SUPPORT_DV_AUDIO enabled=%d phase=\"%.20s\" listen=%s qualified=%u protected=%u unqualified=%u frames=%llu speech=%llu errors=%llu "
      "underruns=%llu overruns=%llu nfm_blocked=%u synth_max_us=%u pump_max_us=%u",
      enabled() ? 1 : 0, dv_voice::phase_name(p.engine.phase), dv_voice::listen_name(p.engine.listen),
      static_cast<unsigned>(vs.qualified), static_cast<unsigned>(vs.protected_groups), static_cast<unsigned>(vs.unqualified),
      (unsigned long long)s.frames, (unsigned long long)s.frames_speech, (unsigned long long)s.frames_error,
      (unsigned long long)s.underruns, (unsigned long long)s.overruns, static_cast<unsigned>(p.nfm_blocked_blocks),
      static_cast<unsigned>(s.synth_us_max), static_cast<unsigned>(p.max_pump_us));
}

namespace {
// One real recorded superframe (terra_153p068_call_cc8, TS1, source 4106008): 15 codec silence frames and 3 speech frames.
const uint8_t kKnownGroup[162] = {
    0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B, 0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B,
    0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B, 0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B,
    0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B, 0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B,
    0x9B, 0xCC, 0x83, 0x74, 0xC8, 0xED, 0x29, 0x8B, 0xFF, 0x9B, 0xCC, 0x83, 0x74, 0xC9, 0xFF, 0x18, 0xAA, 0xFF,
    0x99, 0xEE, 0x83, 0x74, 0xEC, 0xBD, 0x2D, 0x8A, 0xFF, 0xC4, 0x99, 0xA5, 0x26, 0x7C, 0xBA, 0xCD, 0x30, 0x62,
    0xEA, 0x41, 0xC0, 0x57, 0x31, 0xF4, 0x07, 0x75, 0x80, 0x8E, 0x6C, 0xE1, 0x43, 0x31, 0xA4, 0x49, 0xED, 0x52,
    0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B, 0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B,
    0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B, 0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B,
    0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B, 0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B,
};
}  // namespace

int selftest(char* report, size_t size) {
  int fail = 0, checks = 0;
  char detail[160] = "";
  auto check = [&](bool ok, const char* what) {
    ++checks;
    if (!ok) { ++fail; std::snprintf(detail + std::strlen(detail), sizeof(detail) - std::strlen(detail), " FAIL:%s", what); }
  };
  void* mem = alloc(sizeof(Engine));
  Engine* e = mem ? new (mem) Engine() : nullptr;
  dv_voice::Config cfg;
  cfg.ring_ms = 800;
  cfg.target_buffer_ms = 0;
  const bool ok = e && e->begin(cfg);
  check(ok, "alloc");
  uint32_t speech = 0, silence = 0, errors = 0;
  if (ok) {
    e->set_enabled(true);
    dv_radio::VoiceMsg m;
    m.kind = dv_radio::VoiceKind::group; m.slot = 1; m.call_serial = 1; m.new_call = true; m.colour_code = 8;
    std::memcpy(m.payload, kKnownGroup, sizeof(kKnownGroup));
    e->handle(m, 1000);
    auto st = e->snapshot(1000).stats;
    speech = static_cast<uint32_t>(st.frames_speech); silence = static_cast<uint32_t>(st.frames_silence); errors = static_cast<uint32_t>(st.frames_error);
    check(st.frames == 18 && speech == 3 && silence == 15 && errors == 0, "known_group_counts");
    check(e->buffered_samples() == 17280, "group_samples");
    check(st.peak_last > 200 && st.peak_last < 32000 && st.clipped_samples == 0, "speech_level");
    int16_t out[960];
    size_t got = e->pull(out, 960, 1001);
    check(got == 960, "pull");
    e->set_listen(dv_voice::Listen::ts2);
    m.new_call = false; m.slot = 1;
    e->handle(m, 1100);
    check(e->buffered_samples() == 0 && e->snapshot(1100).stats.groups_dropped_not_selected == 1, "selection_drops_other_slot");
    e->set_listen(dv_voice::Listen::mute);
    m.slot = 2;
    e->handle(m, 1200);
    check(e->buffered_samples() == 0, "mute_drops_all");
    e->set_listen(dv_voice::Listen::automatic);
    dv_radio::VoiceMsg pr; pr.kind = dv_radio::VoiceKind::protected_voice; pr.slot = 2;
    e->handle(pr, 1300);
    check(e->snapshot(1300).phase == dv_voice::Phase::protected_voice, "protected_phase");
    // Resampler: 1 kHz tone, 20 frames, level and frequency preserved.
    int16_t* tone = static_cast<int16_t*>(alloc(sizeof(int16_t) * (160 * 20 + 960 * 20)));
    int16_t* o2 = tone + 160 * 20;
    check(tone != nullptr, "alloc_tone");
    if (!tone) { e->end(); e->~Engine(); release(e); std::snprintf(report, size, "RTL_DV_AUDIO_SELFTEST checks=%d failed=%d%s", checks, fail, detail); return fail; }
    for (int i = 0; i < 160 * 20; ++i) tone[i] = static_cast<int16_t>(8000.0 * std::sin(2.0 * 3.14159265358979 * 1000.0 * i / 8000.0));
    e->reset_stream();
    check(e->inject_test_pcm(tone, 160 * 20, 2000), "inject");
    got = e->pull(o2, 960 * 20, 2001);
    check(got == 960 * 20, "tone_len");
    double sq = 0; int zc = 0;
    for (size_t i = 960; i + 960 < got; ++i) { sq += double(o2[i]) * o2[i]; if ((o2[i - 1] < 0) != (o2[i] < 0)) ++zc; }
    const double rms = std::sqrt(sq / double(got - 1920)), secs = double(got - 1920) / 48000.0;
    check(std::fabs(rms - 8000.0 / 1.41421356) < 300.0, "tone_rms");
    check(std::fabs(zc / (2.0 * secs) - 1000.0) < 40.0, "tone_freq");
    release(tone);
    e->end();
  }
  if (e) { e->~Engine(); release(e); }
  std::snprintf(report, size, "RTL_DV_AUDIO_SELFTEST checks=%d failed=%d known_group_speech=%u silence=%u errors=%u%s", checks, fail,
                static_cast<unsigned>(speech), static_cast<unsigned>(silence), static_cast<unsigned>(errors), detail);
  return fail;
}

bool handle_command(const char* arg, bool authenticated, void (*write_line)(const char*)) {
  if (!arg || !write_line || std::strncmp(arg, "AUDIO", 5) != 0) return false;
  const char* op = arg + 5;
  while (*op == ' ') ++op;
  char line[900];
  if (!*op || std::strcmp(op, "STATUS") == 0) {
    format_status(line, sizeof(line));
    write_line(line);
    return true;
  }
  if (!authenticated) { write_line("RTL_DV_AUDIO_ERROR auth_required"); return true; }
  if (std::strcmp(op, "ENABLE") == 0) { set_enabled(true); write_line("RTL_DV_AUDIO_OK enabled"); return true; }
  if (std::strcmp(op, "DISABLE") == 0) { set_enabled(false); reset(); write_line("RTL_DV_AUDIO_OK disabled"); return true; }
  if (std::strcmp(op, "MUTE") == 0) { set_listen(dv_voice::Listen::mute); write_line("RTL_DV_AUDIO_OK listen=MUTE"); return true; }
  if (std::strcmp(op, "UNMUTE") == 0) { set_listen(dv_voice::Listen::automatic); write_line("RTL_DV_AUDIO_OK listen=AUTO"); return true; }
  if (std::strncmp(op, "GAIN", 4) == 0) {                  // playback gain for decoded speech: RTL_DV AUDIO GAIN [0.25..16]
    if (op[4] == ' ') {
      char* end = nullptr;
      const float g = std::strtof(op + 5, &end);
      if (end == op + 5 || *end != 0 || !(g >= 0.25f && g <= 16.0f)) { write_line("RTL_DV_AUDIO_INVALID use RTL_DV AUDIO GAIN <0.25..16>"); return true; }
      set_gain(g);
    }
    std::snprintf(line, sizeof(line), "RTL_DV_AUDIO_OK gain=%.2f (decoded speech playback only; recordings stay raw)", static_cast<double>(gain()));
    write_line(line);
    return true;
  }
  if (std::strncmp(op, "BUFFER ", 7) == 0) {
    const int ms = std::atoi(op + 7);
    if (ms < 0 || ms > 1000) { write_line("RTL_DV_AUDIO_INVALID use RTL_DV AUDIO BUFFER <0..1000 ms>"); return true; }
    set_buffer_ms(static_cast<uint32_t>(ms));
    std::snprintf(line, sizeof(line), "RTL_DV_AUDIO_OK buffer_ms=%d", ms);
    write_line(line);
    return true;
  }
  if (std::strcmp(op, "SAVE ON") == 0 || std::strcmp(op, "SAVE OFF") == 0) {
    set_save_speech(op[5] == 'O' && op[6] == 'N');
    write_line(save_speech() ? "RTL_DV_AUDIO_OK save_speech=on (decoded DMR speech only)" : "RTL_DV_AUDIO_OK save_speech=off");
    return true;
  }
  if (std::strcmp(op, "RESET") == 0) { reset(); write_line("RTL_DV_AUDIO_OK reset"); return true; }
  if (std::strncmp(op, "SLOT ", 5) == 0) {
    const char* s = op + 5;
    if (std::strcmp(s, "AUTO") == 0) set_listen(dv_voice::Listen::automatic);
    else if (std::strcmp(s, "1") == 0) set_listen(dv_voice::Listen::ts1);
    else if (std::strcmp(s, "2") == 0) set_listen(dv_voice::Listen::ts2);
    else { write_line("RTL_DV_AUDIO_INVALID use RTL_DV AUDIO SLOT AUTO|1|2"); return true; }
    std::snprintf(line, sizeof(line), "RTL_DV_AUDIO_OK listen=%s", s);
    write_line(line);
    return true;
  }
  if (std::strncmp(op, "TONE", 4) == 0) {
    const int ms = op[4] == ' ' ? std::atoi(op + 5) : 500;
    write_line(play_tone(static_cast<uint32_t>(ms > 0 ? ms : 500)) ? "RTL_DV_AUDIO_OK tone queued (known-good PCM, speaker-path test, not decoding)"
                                                                   : "RTL_DV_AUDIO_REFUSED (service not running: open the Digital Voice dashboard)");
    return true;
  }
  if (std::strcmp(op, "SELFTEST") == 0) {
    (void)selftest(line, sizeof(line));
    write_line(line);
    return true;
  }
  write_line("RTL_DV_AUDIO_UNKNOWN use STATUS|ENABLE|DISABLE|SLOT AUTO|1|2|MUTE|UNMUTE|RESET|BUFFER <ms>|SAVE ON|OFF|TONE [ms]|SELFTEST");
  return true;
}

}  // namespace orcsdr::dv_voice_service

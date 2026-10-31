#include "dead_air_app.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include <cstdarg>
#include <mbedtls/sha256.h>

#include "dead_air_audio.hpp"
#include "dead_air_clip.hpp"
#include "dead_air_control.hpp"
#include "dead_air_dashboard.hpp"
#include "dead_air_env.hpp"
#include "dead_air_env_m5.hpp"
#include "dead_air_envhistory.hpp"
#include "dead_air_envevents.hpp"
#include "dead_air_journal.hpp"
#include "dead_air_mic.hpp"
#include "dead_air_model.hpp"
#include "dead_air_sd_journal.hpp"
#include "dead_air_stats.hpp"
#include "dead_air_sweep.hpp"
#include "dead_air_veil26.hpp"
#include "dead_air_veil26_log.hpp"
#include <esp_log.h>
#include "memory_reclaim.hpp"
#include "orcsdr_storage.hpp"
#include "screen_controller.hpp"

namespace orcsdr::dead_air_app {
namespace {
using namespace orcsdr::dead_air;

constexpr uint32_t kObserveIntervalMs = 50;     // detector cadence: persistence is 200 ms
constexpr uint32_t kSettleMs = 800;             // after a retune/gain/rate change or receiver (re)start
constexpr uint32_t kSnapshotIntervalMs = 250;   // slowest visible refresh when nothing else changed
constexpr uint32_t kSpectrumFreshMs = 1000;
constexpr uint32_t kHistoryMs = 500;            // one RF strip sample per 500 ms: kRfHistory of them is the last minute
static_assert(kClipInfoSeqs >= kClipMaxTriggers, "ClipInfo must hold every event a clip can cover");
constexpr uint32_t kWorkerStopWaitMs = 1500;
constexpr const char* kVeilIntentions[] = {"DEAD AIR", "ARE YOU THERE", "LISTEN", "ANYONE", "OPEN CHANNEL", "NO QUESTION"};
constexpr uint8_t kVeilIntentionCount = sizeof(kVeilIntentions) / sizeof(kVeilIntentions[0]);
constexpr uint8_t kVeilCustom = 255;            // the intention was set over serial
constexpr uint32_t kMaxClipsPerSession = 40;  // about 40 MB; keeps a busy session from filling the card
constexpr uint64_t kRingMarginSamples = 8192;  // never trust PCM this close to being overwritten by the next DSP block
// Full scale matches the receiver's IQ meter (rtl_signal_dbfs): a CU8 complex sample with both axes
// at the rail, |x| = 127.5 * sqrt(2). Bin values are per-bin power of a Hann-windowed 2048 point FFT.
constexpr float kFullScaleAmplitude = 127.5f * 1.41421356f;

struct Worker {
  std::atomic<bool> stop{false};
  std::atomic<bool> alive{false};
  std::atomic<uint8_t> state{static_cast<uint8_t>(RecorderState::unavailable)};
  std::atomic<uint32_t> session{0};
  std::atomic<uint32_t> saved{0};
  std::atomic<uint32_t> missing{0};
  std::atomic<uint32_t> dropped{0};
  TaskHandle_t task = nullptr;
};

// Level buckets from the DSP thread (single producer) to the UI task (single consumer).
struct AudioRing {
  static constexpr uint32_t kSize = 128;
  AudioBucket slots[kSize];
  std::atomic<uint32_t> head{0};
  AudioMeter meter;  // touched by the DSP thread only
};

// Rolling copy of one source's 48 kHz PCM for event clips (PSRAM). Exactly one thread writes a given ring.
struct PcmRing {
  int16_t* pcm = nullptr;
  uint32_t capacity = 0;                 // samples
  std::atomic<uint64_t> written{0};      // total samples ever written
  std::atomic<uint64_t> run_start{0};    // first sample of the current uninterrupted run of audio
  std::atomic<bool> capture{false};
  std::atomic<uint32_t> last_write_ms{0};
};

// Hand-off between the UI task (plans clips) and the SD worker (writes them).
struct ClipTicket {
  ClipJob job;
  bool abandon = false;                  // the planner gave up: record why, write no file
  const char* reason = nullptr;          // static string
  char demod[8]{};
  uint32_t frequency_hz = 0;
  bool speaker_on = false;
  bool verify = false;                   // re-check an existing clip against its recorded hash instead of writing one
  bool load = false;                     // verify, and also read the PCM into the replay buffer
  bool log_play = false;                 // write a clip_replay journal record
  bool played_to_end = false;            // with log_play: the replay ran to the end
  char verify_name[32]{};
  char verify_sha[65]{};
};
struct ClipResult {
  bool ok = false;
  bool abandoned = false;
  bool truncated = false;
  bool is_verify = false;
  bool is_load = false;
  bool is_log = false;
  uint8_t verify_result = 0;             // 2 matches, 3 mismatch, 4 file missing, 5 clip too long to replay
  char name[32]{};
  char sha[65]{};
  uint32_t samples = 0;
  uint32_t pre_samples = 0;              // from the clip's start to its first trigger
  char demod[8]{};
  uint32_t frequency_hz = 0;
  bool speaker_on = false;
  uint32_t seqs[kClipMaxTriggers]{};
  uint8_t seq_count = 0;
};
struct ClipMail {
  std::atomic<uint8_t> state{0};         // 0 empty, 1 ticket ready, 2 worker busy, 3 result ready
  ClipTicket ticket;
  ClipResult result;
  int16_t chunk[4096];
};

// Everything one clip source needs: its PCM ring, planner and hand-off to the SD worker.
struct ClipChannel {
  PcmRing ring;
  ClipPlanner planner;
  ClipTicket pending_meta;               // receiver details captured when the current clip began
  ClipMail mail;
  bool enabled = true;
  bool flowing = false;                  // PCM for this source is arriving
  bool mic = false;
  const char* source = "receiver_demod";
  const char* tag = "";                  // file name suffix
  std::atomic<uint32_t> saved{0}, failed{0};
  uint32_t refused_no_audio = 0, refused_busy = 0, refused_limit = 0;
};

// One event's trace (see ScrubView): index kTracePre - 1 is the sample taken when the event was logged.
struct EventTrace {
  uint32_t seq = 0;                       // 0 = unused
  uint8_t post = 0;                       // samples collected after the event
  int16_t rf[kTraceSamples]{};
  int16_t audio[kTraceSamples]{};
};

// Journal lines other than events and clips (control result, session summary), posted by the UI task and written by the
// SD worker, which alone owns the file.
struct NoteQueue {
  static constexpr uint32_t kSlots = 4;
  char line[kSlots][320];
  uint16_t length[kSlots]{};
  std::atomic<uint32_t> head{0}, tail{0};
};

struct State {
  EventModel model;
  EventQueue queue;
  Worker worker;
  SdJournalSink sink;
  EventJournal journal;
  Snapshot snapshot;
  float spectrum[kBins]{};
  uint32_t spectrum_ms = 0;
  bool spectrum_have = false;
  uint32_t spectrum_seq = 0;
  bool rx_settled = false;  // receiver ready and past its settling window (refreshed every tick)
  uint32_t opened_ms = 0;
  uint32_t last_observe_ms = 0, last_snapshot_ms = 0, settle_until_ms = 0;
  uint32_t last_signature = 0, config_revision = 0, last_counter = 0;
  bool have_signature = false;
  float last_level = 0.0f;
  bool last_level_valid = false;
  uint32_t last_total = 0;
  AudioRing audio;
  ClipChannel rx;                         // the receiver's demodulated audio
  ClipChannel mic;                        // the Tab5 microphones: opt-in, off at every session start
  uint8_t mic_state = 0;                  // 0 off, 1 listening, 2 paused (speaker on), 3 failed
  const char* mic_error = nullptr;
  bool mic_used = false;                  // the mic was started at least once this session
  ClipInfo recent[kClipInfoCount];
  char recent_sha[kClipInfoCount][65]{};  // full hashes, for re-checking
  uint8_t recent_count = 0;
  uint8_t verify_state[kClipInfoCount]{}; // per clip: 0 not yet, 1 running, 2 matches, 3 mismatch, 4 missing
  uint8_t selected = 0;                   // clip chosen on the Review tab (index into recent)
  bool pinned = false;                    // the owner chose a clip; new clips do not move the selection
  char op_name[32]{};                     // clip the in-flight verify/load applies to
  int16_t* replay_pcm = nullptr;          // the mic ring's buffer, lent to the SD worker for a replay (never live together)
  uint32_t replay_cap = 0;                // samples
  bool mic_resume = false;                // the mic was running when a replay took its buffer: start it again afterwards
  uint32_t mic_resume_due_ms = 0;
  uint32_t replay_samples = 0;            // loaded
  uint8_t replay_state = 0;               // 0 idle, 1 loading, 2 playing, 3 not played
  const char* replay_note = "";
  uint32_t replay_started_ms = 0;
  bool log_pending = false, log_completed = false;
  uint32_t mic_restart_due_ms = 0;        // restart the mic input after a replay (the speaker shares its I2S port)
  uint32_t mic_last_restart_ms = 0;
  uint8_t mic_restarts = 0;               // consecutive watchdog restarts without recovery
  char log_name[32]{};
  uint32_t seq_hist[16]{};                // events that have a clip, newest first
  uint8_t seq_hist_count = 0;
  AudioDetector audio_detector;
  uint32_t audio_tail = 0;
  float audio_env[kBins]{};
  size_t audio_env_count = 0;
  float audio_rms = kAudioSilenceDbfs, audio_peak = kAudioSilenceDbfs;
  uint32_t last_audio_ms = 0;
  bool audio_flowing = false;
  Page last_page = Page::overview;
  Snapshot pushed;  // what the dashboard was last given; refresh decisions compare against this
  uint32_t last_level_refresh_ms = 0;
  Sweep sweep;
  uint8_t sweep_preset = 0;
  uint16_t sweep_dwell_ms = 500;
  int16_t hist[kRfHistory]{};             // the last minute of RF level for the Overview strip, oldest first
  uint8_t hist_mark[kRfHistory]{};
  uint8_t hist_count = 0;
  uint32_t hist_ms = 0, hist_seq = 0, hist_total = 0;
  // Context around each logged event for the scrub view: a level every kTraceStepMs, kept per event.
  EventTrace traces[kEventCapacity];      // slot = event sequence % kEventCapacity, like the event ring
  int16_t fine_rf[kTracePre]{}, fine_audio[kTracePre]{};  // the most recent samples, oldest first
  uint8_t fine_count = 0;
  uint32_t fine_ms = 0;
  uint32_t scrub_seq = 0;                 // the event whose trace the open scrub view shows (0 = none)
  ControlRun control;                     // false-alarm check: three detectors, one per sensitivity
  bool control_was_running = false;
  SessionStats stats;
  env::EnvSensor env_sensor;              // ENV III on Port A (SHT30 + QMP6988); polled from tick() while the screen is open
  bool env_bus = false;                   // the Port A bus could be started (not the same as a module being present)
  EnvEventDetector env_events;            // temperature / humidity / pressure leaving their own baseline
  EnvHistory env_history;                 // one-minute averages for the Environment charts (this State lives in PSRAM)
  uint32_t env_chart_seq = 0, env_chart_ms = 0;  // the history revision the dashboard was last given, and when
  uint32_t ring_retry_ms = 0;             // last attempt to allocate the receiver clip ring after open() could not
  uint32_t env_last_at[3]{};              // sample time last fed to env_events, per channel (each sample is fed once)
  bool env_seen[3]{};
  BandMapView map;                        // the last sweep's measured level per channel
  NoteQueue notes;                        // extra journal lines (control result, session summary) for the SD worker
  bool chime_on = false;
  bool chime_active = false;              // the tone is playing on the replay channel
  uint32_t chime_start_ms = 0, chime_last_ms = 0;
  int16_t* chime_pcm = nullptr;           // PSRAM, built the first time the chime is switched on
  uint32_t chime_samples = 0;
  // VEIL-26: optional symbolic analysis of the measured level (see dead_air_veil26.hpp). Off at every session start; while
  // it is off nothing runs and nothing is written. UI-task fields first, then the worker-owned file.
  veil26::Stream veil;
  bool veil_on = false;
  bool veil_cfg_pending = false;          // the config record could not be queued yet
  runes::Alphabet veil_alphabet = runes::Alphabet::latin;
  uint8_t veil_intent = 0;                // index into kVeilIntentions, or kVeilCustom
  uint32_t veil_inspect_seq = 0;          // event the VEIL-26 overlay shows (0 = the live stream)
  VeilFrameView veil_live;
  VeilFrameView veil_events[kEventCapacity];  // slot = event sequence % kEventCapacity, like the event ring
  veil26::RecordQueue veil_queue;         // UI task -> SD worker, bounded; a full queue drops and counts
  std::atomic<uint32_t> veil_written{0}, veil_write_failures{0};
  std::atomic<bool> veil_logging{false};  // the interpretation file is open
  storage::File veil_file;                // worker only
  bool veil_failed = false;               // worker only: the file could not be opened or written
};

Hooks g_hooks;
State* g_state = nullptr;
bool g_open = false;
std::atomic<AudioRing*> g_ring{nullptr};
std::atomic<PcmRing*> g_rx_ring{nullptr};   // DSP thread writes the receiver audio here
std::atomic<bool> g_replaying{false};        // a clip is playing through the speaker
std::atomic<uint32_t> g_rx_writers{0};      // DSP-thread calls currently inside offer_audio's ring write
std::atomic<PcmRing*> g_mic_ring{nullptr};  // the mic task writes room audio here
std::atomic<bool> g_audio_wanted{false};
uint8_t g_last_action = 0;     // diagnostic: last touch action kind seen by the app
uint32_t g_touches = 0;

bool ready_hooks() {
  return g_hooks.receiver_ready && g_hooks.frequency_hz && g_hooks.demod && g_hooks.sample_rate &&
         g_hooks.config_signature && g_hooks.rf_sample_counter && g_hooks.rf_level_dbfs;
}

void set_state(Worker& w, RecorderState s) { w.state.store(static_cast<uint8_t>(s), std::memory_order_release); }


// ---- Clip writing (SD worker only) -------------------------------------------------------------------------------
void json_append(char* buf, size_t cap, size_t& len, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
void json_append(char* buf, size_t cap, size_t& len, const char* fmt, ...) {
  if (len >= cap) return;
  va_list args;
  va_start(args, fmt);
  const int n = std::vsnprintf(buf + len, cap - len, fmt, args);
  va_end(args);
  if (n > 0) len = std::min(cap - 1, len + static_cast<size_t>(n));
}

void json_seqs(char* buf, size_t cap, size_t& len, const ClipJob& job) {
  json_append(buf, cap, len, "[");
  for (uint8_t i = 0; i < job.seq_count; ++i) json_append(buf, cap, len, "%s%lu", i ? "," : "", static_cast<unsigned long>(job.trigger_seqs[i]));
  json_append(buf, cap, len, "]");
}

bool journal_line(State& s, const char* line, size_t len) {
  return len > 0 && s.sink.append(line, len) && s.sink.sync();
}

// Writes one clip as a WAV file plus one journal record. Every outcome is recorded: a clip that could not be written
// leaves a clip_failed record naming the reason, never a missing or half-valid file.
void write_clip(State& s, ClipChannel& ch) {
  ClipMail& m = ch.mail;
  const ClipTicket& t = m.ticket;
  ClipResult& r = m.result;
  r = ClipResult{};
  char line[896];
  size_t len = 0;
  const auto fail = [&](const char* reason) {
    r.ok = false;
    r.abandoned = true;
    len = 0;
    json_append(line, sizeof(line), len, "{\"v\":1,\"type\":\"clip_failed\",\"source\":\"%s\",\"event_seqs\":", ch.source);
    json_seqs(line, sizeof(line), len, t.job);
    json_append(line, sizeof(line), len, ",\"trigger_count\":%lu,\"reason\":\"%s\"}\n", static_cast<unsigned long>(t.job.trigger_count), reason);
    (void)journal_line(s, line, len);
    ch.failed.fetch_add(1, std::memory_order_relaxed);
  };
  if (t.abandon) { fail(t.reason ? t.reason : "abandoned"); return; }
  PcmRing& ring = ch.ring;
  const ClipJob& job = t.job;
  const uint32_t samples = static_cast<uint32_t>(job.end - job.start);
  storage::FileSystem& fs = storage::filesystem();
  if (!ring.pcm || !(fs.exists("/orcsdr/dead_air/clips") || fs.mkdir("/orcsdr/dead_air/clips"))) { fail("no_clip_folder"); return; }
  std::snprintf(r.name, sizeof(r.name), "s%010lu_e%lu%s.wav", static_cast<unsigned long>(s.sink.session_id()),
                static_cast<unsigned long>(job.trigger_seqs[0]), ch.tag);
  char path[96];
  std::snprintf(path, sizeof(path), "/orcsdr/dead_air/clips/%s", r.name);
  storage::File file = fs.open(path, FILE_WRITE, true);
  if (!file) { fail("file_open_failed"); return; }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts(&sha, 0) == 0;
  uint8_t header[kWavHeaderBytes];
  build_wav_header(header, samples, kClipSampleRate);
  ok = ok && file.write(header, sizeof(header)) == sizeof(header) && mbedtls_sha256_update(&sha, header, sizeof(header)) == 0;
  const char* reason = "write_failed";
  uint64_t pos = job.start;
  const uint32_t cap = ring.capacity;
  int32_t lo = 32767, hi = -32768;  // range of the samples written; a mic clip with none is a stalled input
  while (ok && pos < job.end) {
    const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(sizeof(m.chunk) / sizeof(m.chunk[0]), job.end - pos));
    // The ring keeps overwriting its oldest samples; a sample is still intact only while it is less than one ring behind.
    if (ring.written.load(std::memory_order_acquire) - pos > cap - kRingMarginSamples) { ok = false; reason = "ring_overrun"; break; }
    const uint32_t from = static_cast<uint32_t>(pos % cap);
    const uint32_t first = std::min<uint32_t>(n, cap - from);
    std::memcpy(m.chunk, ring.pcm + from, first * sizeof(int16_t));
    if (n > first) std::memcpy(m.chunk + first, ring.pcm, (n - first) * sizeof(int16_t));
    if (ring.written.load(std::memory_order_acquire) - pos > cap - kRingMarginSamples) { ok = false; reason = "ring_overrun"; break; }
    for (uint32_t i = 0; i < n; ++i) { lo = std::min<int32_t>(lo, m.chunk[i]); hi = std::max<int32_t>(hi, m.chunk[i]); }
    const size_t bytes = static_cast<size_t>(n) * sizeof(int16_t);
    ok = file.write(reinterpret_cast<const uint8_t*>(m.chunk), bytes) == bytes &&
         mbedtls_sha256_update(&sha, reinterpret_cast<const uint8_t*>(m.chunk), bytes) == 0;
    pos += n;
  }
  uint8_t digest[32]{};
  ok = ok && mbedtls_sha256_finish(&sha, digest) == 0;
  mbedtls_sha256_free(&sha);
  const size_t expected = kWavHeaderBytes + static_cast<size_t>(samples) * sizeof(int16_t);
  ok = ok && file.size() == expected && file.sync();
  file.close();
  if (ok && ch.mic && lo == hi) { ok = false; reason = "mic_silent"; }  // identical samples throughout: not a recording of the room
  if (!ok) {
    (void)fs.remove(path);  // never leave a partial clip that looks like evidence
    fail(reason);
    return;
  }
  for (int i = 0; i < 32; ++i) std::snprintf(r.sha + i * 2, 3, "%02x", digest[i]);
  r.ok = true;
  r.truncated = job.truncated;
  r.samples = samples;
  r.pre_samples = job.trigger_sample >= job.start ? static_cast<uint32_t>(job.trigger_sample - job.start) : 0u;
  std::memcpy(r.demod, t.demod, sizeof(r.demod));
  r.frequency_hz = t.frequency_hz;
  r.speaker_on = t.speaker_on;
  r.seq_count = job.seq_count;
  std::memcpy(r.seqs, job.trigger_seqs, sizeof(r.seqs));
  const char* processing = ch.mic ? "none by Dead Air; Tab5 built-in microphone, mono, M5Unified default gain; taken only while the speaker was silent"
                                  : "none by Dead Air; receiver demodulator output";
  len = 0;
  json_append(line, sizeof(line), len, "{\"v\":1,\"type\":\"clip\",\"event_seqs\":");
  json_seqs(line, sizeof(line), len, job);
  json_append(line, sizeof(line),
              len,
              ",\"trigger_count\":%lu,\"file\":\"clips/%s\",\"bytes\":%lu,\"sha256\":\"%s\",\"format\":\"pcm_s16le\","
              "\"rate_hz\":%lu,\"channels\":1,\"samples\":%lu,\"duration_ms\":%lu,\"pre_ms\":%lu,\"post_ms\":%lu,"
              "\"truncated\":%s,\"source\":\"%s\",\"demod\":\"%s\",\"frequency_hz\":%lu,\"speaker\":\"%s\","
              "\"processing\":\"%s\",\"trigger_mono_ms\":%lu,\"unix_time\":",
              static_cast<unsigned long>(job.trigger_count), r.name, static_cast<unsigned long>(expected), r.sha,
              static_cast<unsigned long>(kClipSampleRate), static_cast<unsigned long>(samples),
              static_cast<unsigned long>(static_cast<uint64_t>(samples) * 1000u / kClipSampleRate),
              static_cast<unsigned long>((job.trigger_sample - job.start) * 1000u / kClipSampleRate),
              static_cast<unsigned long>((job.end - job.trigger_sample) * 1000u / kClipSampleRate), job.truncated ? "true" : "false",
              ch.source, t.demod, static_cast<unsigned long>(t.frequency_hz), t.speaker_on ? "on" : "muted", processing,
              static_cast<unsigned long>(job.trigger_ms));
  if (job.unix_time) json_append(line, sizeof(line), len, "%lu}\n", static_cast<unsigned long>(job.unix_time));
  else json_append(line, sizeof(line), len, "null}\n");
  if (!journal_line(s, line, len)) {
    r.ok = false;  // the file exists on the card but the log could not say so: report it as failed rather than half-done
    ch.failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  ch.saved.fetch_add(1, std::memory_order_relaxed);
}

// Re-reads a clip from the card and compares its SHA-256 with the one recorded when it was written.
void verify_clip(State& s) {
  ClipMail& m = s.rx.mail;
  ClipResult& r = m.result;
  r = ClipResult{};
  r.is_verify = true;
  char path[96];
  std::snprintf(path, sizeof(path), "/orcsdr/dead_air/clips/%s", m.ticket.verify_name);
  storage::File file = storage::filesystem().open(path, FILE_READ);
  if (!file) { r.verify_result = 4; return; }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts(&sha, 0) == 0;
  for (;;) {
    const size_t n = file.read(reinterpret_cast<uint8_t*>(m.chunk), sizeof(m.chunk));
    if (n == 0) break;
    ok = ok && mbedtls_sha256_update(&sha, reinterpret_cast<const uint8_t*>(m.chunk), n) == 0;
  }
  uint8_t digest[32]{};
  ok = ok && mbedtls_sha256_finish(&sha, digest) == 0;
  mbedtls_sha256_free(&sha);
  file.close();
  char hex[65]{};
  for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
  r.verify_result = ok && std::strcmp(hex, m.ticket.verify_sha) == 0 ? 2 : 3;
}

// Like verify_clip, and also copies the clip's samples into the replay buffer so a matching clip can be played.
void load_clip(State& s) {
  ClipMail& m = s.rx.mail;
  ClipResult& r = m.result;
  r = ClipResult{};
  r.is_load = true;
  std::snprintf(r.name, sizeof(r.name), "%s", m.ticket.verify_name);
  char path[96];
  std::snprintf(path, sizeof(path), "/orcsdr/dead_air/clips/%s", m.ticket.verify_name);
  storage::File file = storage::filesystem().open(path, FILE_READ);
  if (!file) { r.verify_result = 4; return; }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts(&sha, 0) == 0;
  uint8_t* out = reinterpret_cast<uint8_t*>(s.replay_pcm);
  const size_t out_cap = static_cast<size_t>(s.replay_cap) * sizeof(int16_t);
  size_t offset = 0;
  bool too_long = false;
  for (;;) {
    const size_t n = file.read(reinterpret_cast<uint8_t*>(m.chunk), sizeof(m.chunk));
    if (n == 0) break;
    ok = ok && mbedtls_sha256_update(&sha, reinterpret_cast<const uint8_t*>(m.chunk), n) == 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(m.chunk);
    for (size_t i = 0; i < n; ++i, ++offset) {
      if (offset < kWavHeaderBytes) continue;
      const size_t at = offset - kWavHeaderBytes;
      if (at >= out_cap) { too_long = true; break; }
      out[at] = bytes[i];
    }
  }
  uint8_t digest[32]{};
  ok = ok && mbedtls_sha256_finish(&sha, digest) == 0;
  mbedtls_sha256_free(&sha);
  file.close();
  char hex[65]{};
  for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
  if (too_long) { r.verify_result = 5; return; }
  r.verify_result = ok && std::strcmp(hex, m.ticket.verify_sha) == 0 ? 2 : 3;
  r.samples = offset > kWavHeaderBytes ? static_cast<uint32_t>((offset - kWavHeaderBytes) / sizeof(int16_t)) : 0;
}

void write_replay_log(State& s) {
  const ClipTicket& t = s.rx.mail.ticket;
  s.rx.mail.result = ClipResult{};
  s.rx.mail.result.is_log = true;
  char line[200];
  const int n = std::snprintf(line, sizeof(line), "{\"v\":1,\"type\":\"clip_replay\",\"file\":\"clips/%s\",\"completed\":%s,\"hash_checked\":true}\n",
                              t.verify_name, t.played_to_end ? "true" : "false");
  if (n > 0) (void)journal_line(s, line, static_cast<size_t>(n));
}

void run_ticket(State& s, ClipChannel& ch) {
  if (ch.mail.ticket.load) load_clip(s);
  else if (ch.mail.ticket.log_play) write_replay_log(s);
  else if (ch.mail.ticket.verify) verify_clip(s);
  else write_clip(s, ch);
}

// Writes the journal lines the UI task queued (control result, session summary).
void service_notes(State& s) {
  NoteQueue& q = s.notes;
  for (;;) {
    const uint32_t tail = q.tail.load(std::memory_order_relaxed);
    if (tail == q.head.load(std::memory_order_acquire)) return;
    const uint32_t slot = tail % NoteQueue::kSlots;
    (void)journal_line(s, q.line[slot], q.length[slot]);
    q.tail.store(tail + 1, std::memory_order_release);
  }
}

// Runs whatever the UI task has handed over, for each source.
void service_tickets(State& s) {
  service_notes(s);
  for (ClipChannel* ch : {&s.rx, &s.mic}) {
    if (ch->mail.state.load(std::memory_order_acquire) != 1) continue;
    ch->mail.state.store(2, std::memory_order_relaxed);
    run_ticket(s, *ch);
    ch->mail.state.store(3, std::memory_order_release);
  }
}

void write_clip_summary(State& s) {
  char line[240];
  for (const ClipChannel* ch : {&s.rx, &s.mic}) {
    if (ch->mic && !s.mic_used) continue;  // the mic was never started: nothing to say about it
    const int n = std::snprintf(line, sizeof(line),
                                "{\"v\":1,\"type\":\"clip_summary\",\"source\":\"%s\",\"enabled\":%s,\"saved\":%lu,\"failed\":%lu,"
                                "\"refused_no_audio\":%lu,\"refused_busy\":%lu,\"refused_limit\":%lu}\n",
                                ch->source, ch->enabled ? "true" : "false", static_cast<unsigned long>(ch->saved.load()),
                                static_cast<unsigned long>(ch->failed.load()), static_cast<unsigned long>(ch->refused_no_audio),
                                static_cast<unsigned long>(ch->refused_busy), static_cast<unsigned long>(ch->refused_limit));
    if (n > 0) (void)journal_line(s, line, static_cast<size_t>(n));
  }
}

// VEIL-26 interpretation records go to their own file, never to the measured-event journal. The worker opens it on the first
// queued line, writes a few lines per pass (so storage traffic is bounded by the queue), and gives up on the file for the
// session on the first failure; after that queued lines are discarded and counted, and live detection is untouched.
struct VeilWriter final : veil26::LineWriter {
  storage::File* file;
  explicit VeilWriter(storage::File* f) : file(f) {}
  bool append(const char* line, size_t length) override {
    return file->write(reinterpret_cast<const uint8_t*>(line), length) == length;
  }
};

void service_veil(State& s, size_t max_lines) {
  if (!s.veil_queue.pending()) return;
  if (s.veil_failed) {  // the file is gone for this session: drain so the UI keeps getting honest counters
    char scratch[veil26::kLineBytes];
    size_t n = 0;
    for (size_t i = 0; i < max_lines && s.veil_queue.take(scratch, sizeof(scratch), &n); ++i)
      s.veil_write_failures.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (!s.veil_file) {
    storage::FileSystem& fs = storage::filesystem();
    char path[72];
    const int n = std::snprintf(path, sizeof(path), "/orcsdr/dead_air/veil26_%010lu.jsonl",
                                static_cast<unsigned long>(s.worker.session.load(std::memory_order_acquire)));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(path) || fs.exists(path) || !(s.veil_file = fs.open(path, FILE_APPEND))) {
      s.veil_failed = true;
      s.veil_logging.store(false, std::memory_order_relaxed);
      return;
    }
    s.veil_logging.store(true, std::memory_order_relaxed);
  }
  VeilWriter writer(&s.veil_file);
  bool failed = false;
  const size_t written = veil26::drain(s.veil_queue, writer, max_lines, &failed);
  s.veil_written.fetch_add(static_cast<uint32_t>(written), std::memory_order_relaxed);
  if (failed || (written && !s.veil_file.sync())) {
    s.veil_failed = true;
    s.veil_logging.store(false, std::memory_order_relaxed);
    s.veil_write_failures.fetch_add(1, std::memory_order_relaxed);
  }
}

void finish_veil(State& s) {
  service_veil(s, veil26::RecordQueue::kSlots);  // what the UI task queued just before the stop, summary included
  if (s.veil_file) {
    (void)s.veil_file.sync();
    (void)s.veil_file.close();
  }
  s.veil_file = storage::File();
  s.veil_logging.store(false, std::memory_order_relaxed);
}

// Dedicated, low-priority storage worker. It owns the SD sink and journal; the UI task only
// pushes events into the lock-free queue and reads the health atomics.
void release_audio_buffers(State& s);
void mic_stop(State& s, const char* reason);

void worker_main(void* arg) {
  State* s = static_cast<State*>(arg);
  Worker& w = s->worker;
  bool open = false;
  set_state(w, RecorderState::starting);
  if (g_hooks.sd_ready && g_hooks.sd_ready()) {
    const uint32_t utc = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0;
    // Unique even without a wall clock: SdJournalSink::open skips existing files.
    if (s->sink.open(storage::filesystem(), utc ? utc : 1) && s->journal.start(s->sink, s->sink.session_id())) {
      open = true;
      w.session.store(s->sink.session_id(), std::memory_order_release);
      set_state(w, RecorderState::active);
    }
  }
  if (!open) {
    set_state(w, RecorderState::failed);
    if (s->sink.opened()) (void)s->sink.close();
  }
  while (open && !w.stop.load(std::memory_order_acquire)) {
    s->journal.drain(s->queue, s->sink, 8);
    service_tickets(*s);
    service_veil(*s, 4);  // at most four interpretation lines per 100 ms pass
    w.saved.store(s->journal.saved(), std::memory_order_relaxed);
    w.missing.store(s->journal.missing(), std::memory_order_relaxed);
    w.dropped.store(s->queue.dropped(), std::memory_order_relaxed);
    if (s->journal.failed() || s->sink.failed()) {
      set_state(w, RecorderState::failed);
      open = false;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (open) {
    service_tickets(*s);  // a ticket posted just before the stop
    write_clip_summary(*s);
    finish_veil(*s);
    const bool clean = s->journal.finish(s->queue, s->sink);
    const bool closed = s->sink.close();
    w.saved.store(s->journal.saved(), std::memory_order_relaxed);
    w.missing.store(s->journal.missing(), std::memory_order_relaxed);
    w.dropped.store(s->queue.dropped(), std::memory_order_relaxed);
    set_state(w, clean && closed ? RecorderState::stopped : RecorderState::failed);
  } else if (s->sink.opened()) {
    (void)s->sink.close();
  }
  if (!open) finish_veil(*s);  // the journal failed or never opened: still close any interpretation file and count the rest
  release_audio_buffers(*s);  // the screen is gone and this task was the last reader of the rings
  // The final state (stopped/failed) stays published for the dashboard.
  w.alive.store(false, std::memory_order_release);
  w.task = nullptr;
  vTaskDeleteWithCaps(nullptr);
}

void log_sweep(State& s, Kind kind, uint32_t now) {
  Event e{};
  e.monotonic_ms = now;
  e.unix_time = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0;
  e.source = Source::sweep;
  e.kind = kind;
  const SweepConfig& c = s.sweep.config();
  e.frequency_hz = kind == Kind::sweep_start ? c.start_hz : s.sweep.current_hz();
  e.sweep_end_hz = c.end_hz;
  e.sweep_step_hz = c.step_hz;
  e.sweep_dwell_ms = c.dwell_ms;
  e.sweep_settle_ms = kSweepSettleMs;
  e.sweep_hops = s.sweep.hops();
  e.sweep_unobserved_ms = s.sweep.unobserved_ms();
  s.model.mark_sweep(e);
}

// The band map keeps the strongest level measured on each channel while the receiver dwelt there.
void record_map_level(State& s, uint32_t hz, float level_dbfs) {
  BandMapView& m = s.map;
  if (!m.count || !m.step_hz || hz < m.start_hz) return;
  const uint32_t index = (hz - m.start_hz) / m.step_hz;
  if (index >= m.count) return;
  const int16_t tenths = static_cast<int16_t>(std::clamp<long>(std::lround(level_dbfs * 10.0f), -3000, 3000));
  if (m.level[index] == kHistGap) { m.level[index] = tenths; ++m.visited; }
  else m.level[index] = std::max(m.level[index], tenths);
}

void stop_sweep(State& s, uint32_t now) {
  if (!s.sweep.running()) return;
  s.sweep.stop();
  log_sweep(s, Kind::sweep_stop, now);
  // Leave the receiver where it was before the sweep started.
  if (s.sweep.resume_hz()) {
    if (!(g_hooks.hot_retune && g_hooks.hot_retune(s.sweep.resume_hz())) && g_hooks.tune) g_hooks.tune(s.sweep.resume_hz());
  }
  s.settle_until_ms = now + kSettleMs;
}

bool start_sweep(State& s, uint32_t now) {
  if (s.sweep.running() || s.control.running() || !g_hooks.receiver_ready()) return false;
  SweepConfig c = sweep_preset(s.sweep_preset).config;
  c.dwell_ms = s.sweep_dwell_ms;
  if (!s.sweep.start(c, g_hooks.frequency_hz(), now)) return false;
  // A fresh band map: every channel of this sweep starts as "not measured".
  s.map = BandMapView{};
  s.map.start_hz = c.start_hz;
  s.map.step_hz = c.step_hz;
  s.map.count = static_cast<uint16_t>(std::min<uint32_t>(kBandMapChannels, (c.end_hz - c.start_hz) / c.step_hz + 1u));
  for (size_t i = 0; i < kBandMapChannels; ++i) s.map.level[i] = kHistGap;
  s.stats.on_sweep_start();
  log_sweep(s, Kind::sweep_start, now);
  return true;
}


// ---- Clip planning and the optional microphone (UI task) ---------------------------------------------------------
void post_ticket(ClipChannel& ch, const ClipTicket& t) {
  ch.mail.ticket = t;
  ch.mail.state.store(1, std::memory_order_release);
}

void fill_meta(const ClipChannel& ch, ClipTicket& t) {
  if (ch.mic) std::snprintf(t.demod, sizeof(t.demod), "MIC");
  else std::snprintf(t.demod, sizeof(t.demod), "%s", g_hooks.demod ? g_hooks.demod() : "--");
  t.frequency_hz = !ch.mic && g_hooks.frequency_hz ? g_hooks.frequency_hz() : 0;
  t.speaker_on = g_hooks.sound_enabled ? g_hooks.sound_enabled() : false;
}

// Copies PCM into a ring. Called from exactly one thread per ring (the DSP thread, or the mic task).
void ring_write(PcmRing& r, const int16_t* pcm, size_t count) {
  if (!r.pcm || !r.capacity || !r.capture.load(std::memory_order_relaxed)) return;
  const uint32_t now = millis();
  uint64_t w = r.written.load(std::memory_order_relaxed);
  if (w == 0 || now - r.last_write_ms.load(std::memory_order_relaxed) > 300) r.run_start.store(w, std::memory_order_relaxed);  // a new uninterrupted run
  r.last_write_ms.store(now, std::memory_order_relaxed);
  size_t done = 0;
  while (done < count) {
    const uint32_t from = static_cast<uint32_t>(w % r.capacity);
    const size_t n = std::min<size_t>(count - done, r.capacity - from);
    std::memcpy(r.pcm + from, pcm + done, n * sizeof(int16_t));
    done += n;
    w += n;
  }
  r.written.store(w, std::memory_order_release);
}

// PSRAM for the clip rings, the replay buffer and the chime. Other screens keep large buffers after the user leaves them
// (the LoRa receiver holds 15.8 MB once opened), so a failed allocation asks the registered holders to give back what they
// are not using and tries again (memory_reclaim::alloc_psram). Nothing the user has to do.
void* psram_alloc(size_t bytes) {
  size_t reclaimed = 0;
  void* p = memory_reclaim::alloc_psram(bytes, &reclaimed);
  if (!p || reclaimed)  // the serial log says what was asked, what other screens gave back, and what is left
    ESP_LOGW("dead_air", "PSRAM request %u B %s; holders released %u B; PSRAM free %u B, largest block %u B",
             static_cast<unsigned>(bytes), p ? "granted after reclaim" : "REFUSED", static_cast<unsigned>(reclaimed),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
  return p;
}

// Gives back the audio buffers (clip rings, replay buffer, chime: about 3.5 MB when all are in use) once the screen has
// been left and nothing can touch them. Called by the SD worker as its last act, which is also the last user of the rings;
// close() has already cleared the callback pointers, stopped the mic and any replay or chime. The next open() allocates
// them again.
void release_audio_buffers(State& s) {
  vTaskDelay(pdMS_TO_TICKS(50));  // an audio callback or speaker DMA that loaded a pointer just before close() finishes
  if (g_open) return;             // the screen was reopened meanwhile: the buffers are in use again
  // The DSP thread may be inside ring_write on the receiver ring; wait for it to leave (bounded). If it does not, keep the
  // buffers: the next open() reuses them.
  for (int i = 0; i < 50 && g_rx_writers.load(std::memory_order_acquire) != 0; ++i) vTaskDelay(pdMS_TO_TICKS(10));
  if (g_rx_writers.load(std::memory_order_acquire) != 0 || g_open) return;
  s.replay_pcm = nullptr;  // borrowed from the mic ring: freed with it
  for (int16_t** p : {&s.rx.ring.pcm, &s.mic.ring.pcm, &s.chime_pcm}) {
    int16_t* buffer = *p;
    *p = nullptr;
    if (buffer) heap_caps_free(buffer);
  }
  s.rx.ring.capacity = s.mic.ring.capacity = 0;
  s.replay_cap = 0;
  s.mic_resume = false;
  s.chime_samples = 0;
}

bool alloc_ring(PcmRing& r) {
  if (r.pcm) return true;
  const uint32_t cap = kClipSampleRate / 1000u * kClipRingMs;
  r.pcm = static_cast<int16_t*>(psram_alloc(static_cast<size_t>(cap) * sizeof(int16_t)));
  r.capacity = r.pcm ? cap : 0;
  return r.pcm != nullptr;
}

// Called for every event as it is logged. Only measured detections and investigator marks start a clip.
void trigger_clip(State& s, ClipChannel& ch, const Event& e) {
  (void)s;
  if (!ch.enabled || !ch.ring.pcm) return;
  if (e.kind != Kind::rf_rise && e.kind != Kind::audio_burst && e.kind != Kind::manual_marker) return;
  const bool was_idle = ch.planner.state() == ClipState::idle;
  if (was_idle && ch.saved.load(std::memory_order_relaxed) + ch.failed.load(std::memory_order_relaxed) >= kMaxClipsPerSession) {
    ++ch.refused_limit;  // the event is still logged; only its clip is skipped
    return;
  }
  switch (ch.planner.trigger(ch.ring.written.load(std::memory_order_acquire), ch.ring.run_start.load(std::memory_order_relaxed),
                             ch.flowing, e.sequence, millis(), e.unix_time)) {
    case ClipReject::none:
      if (was_idle) fill_meta(ch, ch.pending_meta);  // what the source was doing when the clip began
      break;
    case ClipReject::no_audio: ++ch.refused_no_audio; break;
    case ClipReject::busy: ++ch.refused_busy; break;
  }
}

void veil_on_event(State& s, const Event& e);

void on_event(void* context, const Event& e) {
  State* s = static_cast<State*>(context);
  enqueue_event(&s->queue, e);  // the journal first: this never depends on audio
  trigger_clip(*s, s->rx, e);
  trigger_clip(*s, s->mic, e);
  veil_on_event(*s, e);         // after the measured record is queued; an interpretation never delays or replaces it
}

void remember_clip(State& s, const ClipChannel& ch, const ClipResult& r) {
  for (size_t i = kClipInfoCount - 1; i > 0; --i) {
    s.recent[i] = s.recent[i - 1];
    std::memcpy(s.recent_sha[i], s.recent_sha[i - 1], sizeof(s.recent_sha[i]));
  }
  ClipInfo& c = s.recent[0];
  c = ClipInfo{};
  std::snprintf(c.name, sizeof(c.name), "%s", r.name);
  std::snprintf(c.sha16, sizeof(c.sha16), "%.16s", r.sha);
  std::snprintf(s.recent_sha[0], sizeof(s.recent_sha[0]), "%s", r.sha);
  c.duration_ds = static_cast<uint16_t>(std::min<uint64_t>(65535, static_cast<uint64_t>(r.samples) * 10u / kClipSampleRate));
  c.truncated = r.truncated;
  std::snprintf(c.demod, sizeof(c.demod), "%s", r.demod);
  c.frequency_hz = r.frequency_hz;
  c.speaker_on = r.speaker_on;
  c.mic = ch.mic;
  c.seq_count = static_cast<uint8_t>(std::min<size_t>(r.seq_count, kClipInfoSeqs));
  std::memcpy(c.event_seqs, r.seqs, c.seq_count * sizeof(uint32_t));
  c.pre_ds = static_cast<uint16_t>(std::min<uint64_t>(65535, static_cast<uint64_t>(r.pre_samples) * 10u / kClipSampleRate));
  s.recent_count = static_cast<uint8_t>(std::min<size_t>(kClipInfoCount, s.recent_count + 1u));
  for (size_t i = kClipInfoCount - 1; i > 0; --i) s.verify_state[i] = s.verify_state[i - 1];
  s.verify_state[0] = 0;  // the newest clip has not been re-checked
  if (s.pinned) s.selected = static_cast<uint8_t>(std::min<size_t>(s.selected + 1u, s.recent_count - 1u));
  else s.selected = 0;
  // Remember which events have a clip (newest first, bounded).
  for (int i = 15; i >= r.seq_count; --i) s.seq_hist[i] = s.seq_hist[i - r.seq_count];
  for (uint8_t i = 0; i < r.seq_count; ++i) s.seq_hist[i] = r.seqs[i];
  s.seq_hist_count = static_cast<uint8_t>(std::min<size_t>(16, s.seq_hist_count + r.seq_count));
}

int find_recent(const State& s, const char* name) {
  for (uint8_t i = 0; i < s.recent_count; ++i)
    if (std::strcmp(s.recent[i].name, name) == 0) return i;
  return -1;
}

// Asks the SD worker to re-read the selected clip and compare its hash. Ignored while the worker is busy.
void request_verify(State& s) {
  if (!s.recent_count || s.replay_state == 1 || s.replay_state == 2) return;
  const uint8_t i = std::min<uint8_t>(s.selected, static_cast<uint8_t>(s.recent_count - 1));
  if (s.verify_state[i] == 1 || s.rx.mail.state.load(std::memory_order_acquire) != 0) return;
  ClipTicket t{};
  t.verify = true;
  std::snprintf(t.verify_name, sizeof(t.verify_name), "%s", s.recent[i].name);
  std::snprintf(t.verify_sha, sizeof(t.verify_sha), "%s", s.recent_sha[i]);
  std::snprintf(s.op_name, sizeof(s.op_name), "%s", s.recent[i].name);
  s.verify_state[i] = 1;
  post_ticket(s.rx, t);
}

void end_replay(State& s, bool completed) {
  if (s.replay_state != 2) return;
  if (g_hooks.replay_stop) g_hooks.replay_stop();
  g_replaying.store(false, std::memory_order_release);
  s.replay_state = 0;
  if (s.mic_resume) s.mic_resume_due_ms = (millis() + 300u) | 1u;  // playback leaves the shared I2S input returning a constant
  s.log_pending = true;  // recorded once the worker is free
  s.log_completed = completed;
  const int i = find_recent(s, s.op_name);
  std::snprintf(s.log_name, sizeof(s.log_name), "%s", i >= 0 ? s.recent[i].name : s.op_name);
}

// Reads the selected clip from the card, checks its hash, and plays it only if it matches.
void request_play(State& s) {
  if (!s.recent_count || s.replay_state == 1 || s.replay_state == 2 || !g_hooks.replay_begin) return;
  if (s.rx.mail.state.load(std::memory_order_acquire) != 0) { s.replay_state = 3; s.replay_note = "Busy writing; try again"; return; }
  if (s.mic.mail.state.load(std::memory_order_acquire) != 0) { s.replay_state = 3; s.replay_note = "Busy writing; try again"; return; }
  // The replay borrows the mic ring's buffer: the mic cannot record while a clip plays anyway. A running mic is stopped for
  // the replay and started again afterwards.
  if (!alloc_ring(s.mic.ring)) { s.replay_state = 3; s.replay_note = "No memory for replay"; return; }
  if (s.mic.enabled) {
    mic_stop(s, "replay");
    s.mic_resume = true;
  }
  s.replay_pcm = s.mic.ring.pcm;
  s.replay_cap = s.mic.ring.capacity;
  const uint8_t i = std::min<uint8_t>(s.selected, static_cast<uint8_t>(s.recent_count - 1));
  ClipTicket t{};
  t.verify = true;
  t.load = true;
  std::snprintf(t.verify_name, sizeof(t.verify_name), "%s", s.recent[i].name);
  std::snprintf(t.verify_sha, sizeof(t.verify_sha), "%s", s.recent_sha[i]);
  std::snprintf(s.op_name, sizeof(s.op_name), "%s", s.recent[i].name);
  s.verify_state[i] = 1;
  s.replay_state = 1;
  s.replay_note = "";
  post_ticket(s.rx, t);
}

// A finished load: the clip plays only when its hash matched what was recorded.
void finish_load(State& s, const ClipResult& r) {
  const int i = find_recent(s, r.name);
  if (i >= 0) s.verify_state[i] = r.verify_result == 5 ? 3 : r.verify_result;
  if (s.replay_state != 1) return;
  if (r.verify_result != 2 || r.samples == 0) {
    s.replay_state = 3;
    s.replay_note = r.verify_result == 4 ? "File missing: not played" : r.verify_result == 5 ? "Clip too long: not played" :
                    r.verify_result == 3 ? "Hash mismatch: not played" : "Empty clip: not played";
    return;
  }
  s.replay_samples = r.samples;
  g_replaying.store(true, std::memory_order_release);  // the radio's audio and the mic stand down first
  if (g_hooks.replay_begin(s.replay_pcm, s.replay_samples)) {
    s.replay_state = 2;
    s.replay_started_ms = millis();
  } else {
    g_replaying.store(false, std::memory_order_release);
    s.replay_state = 3;
    s.replay_note = "Speaker unavailable";
  }
}

// Collects a finished clip from the SD worker, then asks the planner whether another is ready to hand over.
void service_clips(State& s, ClipChannel& ch, uint32_t now) {
  if (ch.mail.state.load(std::memory_order_acquire) == 3) {
    const ClipResult r = ch.mail.result;
    if (r.is_load) finish_load(s, r);
    else if (r.is_log) {}
    else if (r.is_verify) {
      const int i = find_recent(s, s.op_name);
      if (i >= 0) s.verify_state[i] = r.verify_result;
    } else {
      if (r.ok) remember_clip(s, ch, r);
      if (ch.planner.state() == ClipState::writing) ch.planner.finished();
    }
    ch.mail.state.store(0, std::memory_order_relaxed);
  }
  if (ch.mail.state.load(std::memory_order_acquire) != 0) return;
  ClipJob job;
  ClipFail failure = ClipFail::none;
  if (ch.planner.poll(ch.ring.written.load(std::memory_order_acquire), ch.ring.run_start.load(std::memory_order_relaxed),
                      ch.flowing, now, &job, &failure)) {
    ClipTicket t{};
    t.job = job;
    std::memcpy(t.demod, ch.pending_meta.demod, sizeof(t.demod));
    t.frequency_hz = ch.pending_meta.frequency_hz;
    t.speaker_on = ch.pending_meta.speaker_on;
    post_ticket(ch, t);
  } else if (failure != ClipFail::none) {
    ClipTicket t{};
    t.job = job;
    t.abandon = true;
    t.reason = "audio_stopped";
    post_ticket(ch, t);
  }
}

// A capture that can no longer complete is recorded as a failure with its reason, not silently dropped.
void abandon_capture(ClipChannel& ch, const char* reason) {
  if (ch.planner.state() == ClipState::capturing && ch.mail.state.load(std::memory_order_acquire) == 0) {
    ClipTicket t{};
    t.job = ch.planner.pending();
    t.abandon = true;
    t.reason = reason;
    post_ticket(ch, t);
  }
  ch.planner.cancel();
}

// Mic task: the microphone only records while the speaker is silent. With radio audio playing it would just record the
// speaker (measured at about -2 dBFS, against about -56 dBFS in a quiet room), so those blocks are dropped and the run
// restarts when the speaker goes quiet.
void mic_sink(const int16_t* pcm, size_t count) {
  PcmRing* ring = g_mic_ring.load(std::memory_order_acquire);
  if (!ring) return;
  if ((g_hooks.sound_enabled && g_hooks.sound_enabled()) || g_replaying.load(std::memory_order_relaxed)) return;
  if (dead_air_mic::flat_blocks() >= 3) return;  // identical samples: the input has stalled, so there is nothing real to keep
  ring_write(*ring, pcm, count);
}

bool mic_start(State& s) {
  if (s.mic.enabled) return true;
  if (s.replay_state == 1 || s.replay_state == 2) { s.mic_error = "playing a clip"; s.mic_state = 3; return false; }  // its buffer is lent out
  if (!alloc_ring(s.mic.ring)) { s.mic_error = "no memory"; s.mic_state = 3; return false; }
  s.mic.ring.written.store(0, std::memory_order_relaxed);
  s.mic.ring.run_start.store(0, std::memory_order_relaxed);
  s.mic.ring.last_write_ms.store(0, std::memory_order_relaxed);
  s.mic.planner.configure(kClipSampleRate, kClipPreMs, kClipPostMs);
  s.mic.mail.state.store(0, std::memory_order_relaxed);
  s.mic.flowing = false;
  s.mic.ring.capture.store(true, std::memory_order_relaxed);
  g_mic_ring.store(&s.mic.ring, std::memory_order_release);
  if (!dead_air_mic::start(mic_sink)) {
    g_mic_ring.store(nullptr, std::memory_order_release);
    s.mic.ring.capture.store(false, std::memory_order_relaxed);
    s.mic_error = dead_air_mic::stats().error ? dead_air_mic::stats().error : "mic failed";
    s.mic_state = 3;
    return false;
  }
  s.mic.enabled = true;
  s.mic_used = true;
  s.mic_error = nullptr;
  s.mic_state = 1;
  return true;
}

// Re-opens the microphone input. Playing a clip on the shared I2S port can leave the input returning one constant value;
// recording then would save silence that looks like evidence.
void mic_restart(State& s, const char* reason) {
  abandon_capture(s.mic, reason);
  dead_air_mic::stop();
  s.mic.flowing = false;
  s.mic_last_restart_ms = millis();
  if (!dead_air_mic::start(mic_sink)) {
    s.mic_error = "mic restart failed";
    s.mic.enabled = false;
    g_mic_ring.store(nullptr, std::memory_order_release);
    s.mic.ring.capture.store(false, std::memory_order_relaxed);
    s.mic_state = 3;
  }
}

void mic_stop(State& s, const char* reason) {
  if (!s.mic.enabled && !dead_air_mic::running()) { if (s.mic_state != 3) s.mic_state = 0; return; }
  abandon_capture(s.mic, reason);
  g_mic_ring.store(nullptr, std::memory_order_release);
  s.mic.ring.capture.store(false, std::memory_order_relaxed);
  dead_air_mic::stop();
  s.mic.enabled = false;
  s.mic.flowing = false;
  s.mic_state = 0;
}

bool ensure_state() {
  if (g_state) return true;
  void* memory = heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) return false;
  g_state = new (memory) State();
  g_state->mic.enabled = false;  // opt-in: never on until the owner starts it
  g_state->mic.mic = true;
  g_state->mic.source = "device_mic";
  g_state->mic.tag = "_mic";
  return true;
}

bool start_worker() {
  Worker& w = g_state->worker;
  const uint32_t begin = millis();
  while (w.alive.load(std::memory_order_acquire) && millis() - begin < kWorkerStopWaitMs) vTaskDelay(pdMS_TO_TICKS(10));
  if (w.alive.load(std::memory_order_acquire)) return false;  // previous worker still closing its file
  w.stop.store(false, std::memory_order_release);
  w.saved = w.missing = w.dropped = 0;
  w.session = 0;
  g_state->sink = SdJournalSink();
  g_state->journal = EventJournal();
  g_state->queue.~EventQueue();  // atomics are not assignable; no worker is alive, so rebuild in place
  new (&g_state->queue) EventQueue();
  g_state->notes.head.store(0, std::memory_order_relaxed);  // no worker is alive: nothing is reading the notes
  g_state->notes.tail.store(0, std::memory_order_relaxed);
  g_state->veil_queue.reset();                              // likewise for the interpretation queue and its file
  g_state->veil_written.store(0, std::memory_order_relaxed);
  g_state->veil_write_failures.store(0, std::memory_order_relaxed);
  g_state->veil_logging.store(false, std::memory_order_relaxed);
  g_state->veil_file = storage::File();
  g_state->veil_failed = false;
  w.alive.store(true, std::memory_order_release);
  set_state(w, RecorderState::starting);
  if (xTaskCreatePinnedToCoreWithCaps(worker_main, "dead_air_sd", 12288, g_state, 1, &w.task, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    w.alive.store(false, std::memory_order_release);
    set_state(w, RecorderState::failed);
    return false;
  }
  return true;
}

// ---- Journal notes, event traces, chime --------------------------------------------------------------------------------
bool post_note(State& s, const char* line, int length) {
  if (length <= 0 || length >= static_cast<int>(sizeof(s.notes.line[0]))) return false;
  if (s.worker.state.load(std::memory_order_acquire) != static_cast<uint8_t>(RecorderState::active)) return false;
  NoteQueue& q = s.notes;
  const uint32_t head = q.head.load(std::memory_order_relaxed);
  if (head - q.tail.load(std::memory_order_acquire) >= NoteQueue::kSlots) return false;  // the worker is behind: drop it
  const uint32_t slot = head % NoteQueue::kSlots;
  std::memcpy(q.line[slot], line, static_cast<size_t>(length));
  q.length[slot] = static_cast<uint16_t>(length);
  q.head.store(head + 1, std::memory_order_release);
  return true;
}

void post_control_note(State& s) {
  const ControlRun& c = s.control;
  char rate[3][16];
  for (int i = 0; i < 3; ++i) {
    if (c.rate_available()) std::snprintf(rate[i], sizeof(rate[i]), "%.1f", static_cast<double>(c.per_hour(static_cast<Sensitivity>(i))));
    else std::snprintf(rate[i], sizeof(rate[i]), "null");
  }
  char line[320];
  const int n = std::snprintf(line, sizeof(line),
      "{\"v\":1,\"type\":\"control_run\",\"complete\":%s,\"duration_s\":%lu,\"observed_s\":%lu,"
      "\"false_alarms\":{\"quiet\":%lu,\"standard\":%lu,\"sensitive\":%lu},"
      "\"per_hour\":{\"quiet\":%s,\"standard\":%s,\"sensitive\":%s},"
      "\"note\":\"declared by the investigator as a setup with nothing to detect; RF only; live detection was paused\"}\n",
      c.complete() ? "true" : "false", static_cast<unsigned long>(c.duration_ms() / 1000u),
      static_cast<unsigned long>(c.observed_ms() / 1000u), static_cast<unsigned long>(c.count(Sensitivity::quiet)),
      static_cast<unsigned long>(c.count(Sensitivity::standard)), static_cast<unsigned long>(c.count(Sensitivity::sensitive)),
      rate[0], rate[1], rate[2]);
  (void)post_note(s, line, n);
}

void post_session_note(State& s, uint32_t now) {
  const SessionSummary m = s.stats.summary(now, s.rx.saved.load(std::memory_order_relaxed) + s.mic.saved.load(std::memory_order_relaxed));
  char hi[16], lo[16];
  if (m.have_level) {
    std::snprintf(hi, sizeof(hi), "%.1f", static_cast<double>(m.level_max_dbfs));
    std::snprintf(lo, sizeof(lo), "%.1f", static_cast<double>(m.level_min_dbfs));
  } else {
    std::snprintf(hi, sizeof(hi), "null");
    std::snprintf(lo, sizeof(lo), "null");
  }
  char line[320];
  const int n = std::snprintf(line, sizeof(line),
      "{\"v\":1,\"type\":\"session_summary\",\"elapsed_s\":%lu,\"measured_s\":%lu,"
      "\"events\":{\"rf\":%lu,\"audio\":%lu,\"marks\":%lu},\"level_max_dbfs\":%s,\"level_min_dbfs\":%s,"
      "\"longest_quiet_s\":%lu,\"channels\":%u,\"sweeps\":%lu,\"clips\":%lu}\n",
      static_cast<unsigned long>(m.elapsed_s), static_cast<unsigned long>(m.measured_s),
      static_cast<unsigned long>(m.events_rf), static_cast<unsigned long>(m.events_audio), static_cast<unsigned long>(m.events_marks),
      hi, lo, static_cast<unsigned long>(m.longest_quiet_s), static_cast<unsigned>(m.channels),
      static_cast<unsigned long>(m.sweeps), static_cast<unsigned long>(m.clips));
  (void)post_note(s, line, n);
}

// ---- VEIL-26 (UI task only) --------------------------------------------------------------------------------------------
// Feeds the engine from the observations Dead Air already makes: no new acquisition, no new detector. A frame is made at
// most once a second from a usable observation (valid, settled, a tuned frequency, a finite level) and once per RF or
// audio event. Anything else is a gap: the stream is not advanced and the break is counted. The engine runs in a few
// dozen integer operations per frame, off every DSP path.
void veil_fill(VeilFrameView& v, const veil26::Frame& f, uint32_t hz, double level, uint32_t t_ms, uint32_t utc,
               uint32_t event_seq, const char* source) {
  v = VeilFrameView{};
  v.valid = true;
  v.frame = f.index;
  v.monotonic_ms = t_ms;
  v.unix_time = utc;
  v.frequency_hz = hz;
  v.event_sequence = event_seq;
  v.level_dbfs = static_cast<float>(level);
  v.freq_code = static_cast<int32_t>(std::clamp<int64_t>(f.codes.frequency, INT32_MIN, INT32_MAX));
  v.level_code = static_cast<int32_t>(std::clamp<int64_t>(f.codes.level, INT32_MIN, INT32_MAX));
  v.state_in = f.state_in;
  v.state = f.state;
  v.letter_number = f.letter_number;
  v.letter = f.letter;
  v.word_index = f.word_index;
  std::snprintf(v.word, sizeof(v.word), "%s", f.word);
  v.a1z26 = static_cast<uint16_t>(f.word_a1z26);
  v.pythagorean = static_cast<uint16_t>(f.word_pythagorean);
  std::snprintf(v.source, sizeof(v.source), "%s", source);
}

bool veil_post_config(State& s, uint32_t now) {
  char line[veil26::kLineBytes];
  const size_t n = veil26::format_config(s.veil.intention(), s.veil.seed(), g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0, now, line, sizeof(line), s.veil_alphabet);
  return n != 0 && s.veil_queue.post(line, n);
}

void veil_post_record(State& s, const veil26::Frame& f, veil26::Trigger trigger, uint32_t event_seq, const char* event_kind,
                      uint32_t hz, double level, const char* source, uint32_t now, uint32_t utc) {
  if (s.veil_cfg_pending) {  // a record is only useful after the config that explains it
    if (!veil_post_config(s, now)) return;  // counted by the queue as dropped when it was full
    s.veil_cfg_pending = false;
  }
  veil26::RecordInput in;
  in.frame = f;
  in.alphabet = s.veil_alphabet;
  in.intention = s.veil.intention();
  in.seed = s.veil.seed();
  in.trigger = trigger;
  in.event_sequence = event_seq;
  in.event_kind = event_kind;
  in.monotonic_ms = now;
  in.unix_time = utc;
  in.frequency_hz = hz;
  in.level_dbfs = level;
  in.level_source = source;
  in.config_revision = s.config_revision;
  char line[veil26::kLineBytes];
  const size_t n = veil26::format_record(in, line, sizeof(line));
  if (n) (void)s.veil_queue.post(line, n);
}

// Starts the stream again from an intention. Nothing carries over: frame counter, letters, live frame and the per-event
// interpretations all restart, and a new config record names the intention for the file.
void veil_restart(State& s, const char* intention) {
  s.veil.restart(intention);
  s.veil_live = VeilFrameView{};
  for (VeilFrameView& v : s.veil_events) v = VeilFrameView{};
  s.veil_cfg_pending = s.veil_on;
}

void veil_observe(State& s, const RfObservation& obs, uint32_t now) {
  if (!s.veil_on) return;
  if (s.veil_cfg_pending && veil_post_config(s, now)) s.veil_cfg_pending = false;
  veil26::Observation o;
  o.now_ms = now;
  o.valid = obs.valid;
  o.settled = obs.settled;
  o.frequency_hz = obs.frequency_hz;
  o.config_revision = obs.configuration_revision;
  o.level_dbfs = static_cast<double>(obs.level_dbfs);
  veil26::Frame f;
  bool log_due = false;
  if (s.veil.observe(o, &f, &log_due) != veil26::Stream::Result::frame) return;
  veil_fill(s.veil_live, f, obs.frequency_hz, obs.level_dbfs, now, obs.unix_time, 0, "rf");
  if (log_due) veil_post_record(s, f, veil26::Trigger::sample, 0, "", obs.frequency_hz, obs.level_dbfs, "rf", now, obs.unix_time);
}

// An RF rise or an audio burst has a tuned frequency and a measured dBFS level: exactly the pair the frequency-log
// convention quantizes. Environment events and marks have no such pair, so they are not interpreted (and say so).
void veil_on_event(State& s, const Event& e) {
  if (!s.veil_on || (e.source != Source::rf && e.source != Source::audio)) return;
  veil26::Frame f;
  if (!s.veil.observe_event(e.frequency_hz, static_cast<double>(e.value), &f)) return;
  const char* source = e.source == Source::rf ? "rf" : "audio";
  veil_fill(s.veil_events[e.sequence % kEventCapacity], f, e.frequency_hz, e.value, e.monotonic_ms, e.unix_time, e.sequence, source);
  s.veil_live = s.veil_events[e.sequence % kEventCapacity];
  veil_post_record(s, f, veil26::Trigger::event, e.sequence, kind_name(e.kind), e.frequency_hz, e.value, source,
                   e.monotonic_ms, e.unix_time);
}

veil26::Counters veil_counters(const State& s) {
  veil26::Counters c;
  c.frames = s.veil.frames();
  c.gaps = s.veil.gaps();
  c.skipped_invalid = s.veil.skipped();
  c.records_posted = s.veil_queue.posted();
  c.records_dropped = s.veil_queue.dropped();
  c.records_written = s.veil_written.load(std::memory_order_relaxed);
  c.write_failures = s.veil_write_failures.load(std::memory_order_relaxed);
  return c;
}

void veil_post_summary(State& s) {
  if (!s.veil_on) return;
  char line[veil26::kLineBytes];
  const size_t n = veil26::format_summary(veil_counters(s), line, sizeof(line));
  if (n) (void)s.veil_queue.post(line, n);
}

// Every kTraceStepMs: one RF and one audio sample into the short ring, and into each trace still collecting its +10 s.
void update_traces(State& s, uint32_t now) {
  if (now - s.fine_ms < kTraceStepMs) return;
  s.fine_ms = now;
  const bool rf_ok = g_hooks.receiver_ready() && s.rx_settled && s.last_level_valid && std::isfinite(s.last_level);
  const int16_t rf = rf_ok ? static_cast<int16_t>(std::clamp<long>(std::lround(s.last_level * 10.0f), -3000, 3000)) : kHistGap;
  const bool au_ok = s.audio_flowing && std::isfinite(s.audio_rms);
  const int16_t au = au_ok ? static_cast<int16_t>(std::clamp<long>(std::lround(s.audio_rms * 10.0f), -3000, 3000)) : kHistGap;
  std::memmove(s.fine_rf, s.fine_rf + 1, (kTracePre - 1) * sizeof(s.fine_rf[0]));
  std::memmove(s.fine_audio, s.fine_audio + 1, (kTracePre - 1) * sizeof(s.fine_audio[0]));
  s.fine_rf[kTracePre - 1] = rf;
  s.fine_audio[kTracePre - 1] = au;
  for (EventTrace& t : s.traces) {
    if (!t.seq || t.post >= kTraceSamples - kTracePre) continue;
    t.rf[kTracePre + t.post] = rf;
    t.audio[kTracePre + t.post] = au;
    ++t.post;
  }
}

// An event was logged: keep the 10 s of context before it (the +10 s after fills in as it happens).
void capture_trace(State& s, const Event& e) {
  EventTrace& t = s.traces[e.sequence % kEventCapacity];
  t.seq = e.sequence;
  t.post = 0;
  for (size_t j = 0; j < kTraceSamples; ++j) t.rf[j] = t.audio[j] = kHistGap;
  std::memcpy(t.rf, s.fine_rf, sizeof(s.fine_rf));
  std::memcpy(t.audio, s.fine_audio, sizeof(s.fine_audio));
}

// A short two-note ping, built once in PSRAM.
bool build_chime(State& s) {
  if (s.chime_pcm) return true;
  constexpr uint32_t kNote = 4800;  // 100 ms per note at 48 kHz
  s.chime_pcm = static_cast<int16_t*>(psram_alloc(2u * kNote * sizeof(int16_t)));
  if (!s.chime_pcm) return false;
  s.chime_samples = 2u * kNote;
  for (uint32_t i = 0; i < s.chime_samples; ++i) {
    const float hz = i < kNote ? 880.0f : 1320.0f;
    const float within = static_cast<float>(i % kNote) / static_cast<float>(kNote);
    const float envelope = (1.0f - within) * std::min(1.0f, static_cast<float>(i % kNote) / 120.0f);  // quick attack, linear decay
    s.chime_pcm[i] = static_cast<int16_t>(5500.0f * envelope * std::sin(6.2831853f * hz * static_cast<float>(i) / static_cast<float>(kClipSampleRate)));
  }
  return true;
}

void end_chime(State& s) {
  if (!s.chime_active) return;
  if (g_hooks.replay_stop) g_hooks.replay_stop();
  g_replaying.store(false, std::memory_order_release);
  s.chime_active = false;
  if (s.mic.enabled) s.mic_restart_due_ms = (millis() + 300u) | 1u;  // playback leaves the shared I2S input stuck, as after a replay
}

// Plays the ping when the owner switched the chime on, the speaker is not muted, and nothing else is using the speaker.
void play_chime(State& s, uint32_t now) {
  if (!s.chime_on || s.chime_active || s.replay_state != 0 || !g_hooks.replay_begin || !s.chime_pcm) return;
  if (!(g_hooks.sound_enabled && g_hooks.sound_enabled())) return;
  if (s.chime_last_ms && now - s.chime_last_ms < 3000) return;  // one ping per few seconds, however busy the band is
  s.chime_last_ms = now;
  g_replaying.store(true, std::memory_order_release);  // the radio's audio and the mic stand down first
  if (g_hooks.replay_begin(s.chime_pcm, s.chime_samples)) {
    s.chime_active = true;
    s.chime_start_ms = now;
  } else {
    g_replaying.store(false, std::memory_order_release);
  }
}

// Overview RF strip: one sample every kHistoryMs, and a mark on the newest sample for each event logged since. A moment
// with no settled, valid level is a gap, so the strip never draws a line across a retune, a sweep or a lost receiver.
void update_history(State& s, uint32_t now) {
  if (now - s.hist_ms >= kHistoryMs) {
    s.hist_ms = now;
    if (s.hist_count == kRfHistory) {
      std::memmove(s.hist, s.hist + 1, (kRfHistory - 1) * sizeof(s.hist[0]));
      std::memmove(s.hist_mark, s.hist_mark + 1, kRfHistory - 1);
      --s.hist_count;
    }
    const bool valid = g_hooks.receiver_ready() && s.rx_settled && s.last_level_valid && std::isfinite(s.last_level);
    const long tenths = valid ? std::lround(s.last_level * 10.0f) : 0;
    s.hist[s.hist_count] = valid ? static_cast<int16_t>(std::clamp<long>(tenths, -3000, 3000)) : kHistGap;
    s.hist_mark[s.hist_count] = static_cast<uint8_t>(HistMark::none);
    ++s.hist_count;
    ++s.hist_seq;
  }
  update_traces(s, now);
  const uint32_t total = s.model.total();
  if (total != s.hist_total) {
    const size_t fresh = std::min<size_t>(total - s.hist_total, s.model.count());
    for (size_t i = fresh; i-- > 0;) {  // oldest first, so the newest event's mark wins
      const Event* e = s.model.newest(i);
      if (!e) continue;
      s.stats.on_event(*e);
      if (e->source != Source::sweep) capture_trace(s, *e);
      dead_air::fx_event(*e, now);  // the orb bursts for RF, audio and marks; sweep records are ignored there
      if (e->kind == Kind::rf_rise || e->kind == Kind::audio_burst) play_chime(s, now);
      HistMark mark = HistMark::none;
      if (e->kind == Kind::rf_rise) mark = HistMark::rf_rise;
      else if (e->kind == Kind::audio_burst) mark = HistMark::audio_burst;
      else if (e->kind == Kind::manual_marker) mark = HistMark::manual;
      if (mark != HistMark::none && s.hist_count) s.hist_mark[s.hist_count - 1] = static_cast<uint8_t>(mark);
    }
    s.hist_total = total;
  }
}

void build_snapshot(uint32_t now) {
  State& s = *g_state;
  Snapshot& n = s.snapshot;
  const bool ready = g_hooks.receiver_ready();
  n.receiver_ready = ready;
  n.receiver_settled = ready && static_cast<int32_t>(now - s.settle_until_ms) >= 0;
  n.frequency_hz = ready ? g_hooks.frequency_hz() : 0;
  n.sample_rate_sps = ready ? g_hooks.sample_rate() : 0;
  std::snprintf(n.demod, sizeof(n.demod), "%s", ready ? g_hooks.demod() : "--");
  n.sound_enabled = g_hooks.sound_enabled ? g_hooks.sound_enabled() : true;
  n.battery_percent = g_hooks.battery_percent ? g_hooks.battery_percent() : -1;
  n.unix_time = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0;
  n.session_elapsed_s = (now - s.opened_ms) / 1000u;
  n.recording = false;     // audio/IQ recording is not implemented
  n.scanning = false;      // unused; sweep state is in `sweeping` below
  std::memcpy(n.rf_history, s.hist, sizeof(n.rf_history));
  std::memcpy(n.rf_history_event, s.hist_mark, sizeof(n.rf_history_event));
  n.rf_history_count = s.hist_count;
  n.rf_history_seq = s.hist_seq;
  n.summary = s.stats.summary(now, s.rx.saved.load(std::memory_order_relaxed) + s.mic.saved.load(std::memory_order_relaxed));
  n.chime_on = s.chime_on;
  {
    VeilView& v = n.veil;
    v.alphabet = s.veil_alphabet;
    v.enabled = s.veil_on;
    v.logging = s.veil_logging.load(std::memory_order_relaxed);
    std::snprintf(v.intention, sizeof(v.intention), "%s", s.veil.intention());
    v.seed = s.veil.seed();
    const veil26::Counters c = veil_counters(s);
    v.frames = c.frames;
    v.gaps = c.gaps;
    v.skipped = c.skipped_invalid;
    v.posted = c.records_posted;
    v.dropped = c.records_dropped;
    v.written = c.records_written;
    v.write_failures = c.write_failures;
    v.live = s.veil_live;
    std::snprintf(v.letters, sizeof(v.letters), "%s", s.veil.letters());
    v.seq_a1z26 = static_cast<uint16_t>(s.veil.sequence_a1z26());
    v.seq_pythagorean = static_cast<uint16_t>(s.veil.sequence_pythagorean());
    v.inspect_seq = s.veil_inspect_seq;
    v.event = VeilFrameView{};
    if (s.veil_inspect_seq) {
      const VeilFrameView& e = s.veil_events[s.veil_inspect_seq % kEventCapacity];
      if (e.valid && e.event_sequence == s.veil_inspect_seq) v.event = e;  // else: that event was not analysed
    }
    std::snprintf(v.dictionary, sizeof(v.dictionary), "%s", veil26::kDictionaryId);
  }
  n.map = s.map;
  ControlView& cv = n.control;
  cv = ControlView{};
  cv.state = s.control.running() ? 1 : s.control.has_result() ? (s.control.complete() ? 2 : 3) : 0;
  cv.duration_s = s.control.duration_ms() / 1000u;
  cv.elapsed_s = s.control.elapsed_ms(now) / 1000u;
  cv.observed_s = s.control.observed_ms() / 1000u;
  cv.rate_ok = s.control.rate_available();
  for (int i = 0; i < 3; ++i) {
    cv.count[i] = s.control.count(static_cast<Sensitivity>(i));
    cv.per_hour[i] = s.control.per_hour(static_cast<Sensitivity>(i));
  }
  ScrubView& sv = n.scrub;
  sv = ScrubView{};
  if (s.scrub_seq && dead_air::overlay() == Overlay::scrub) {
    sv.seq = s.scrub_seq;
    const EventTrace& t = s.traces[s.scrub_seq % kEventCapacity];
    if (t.seq == s.scrub_seq) {
      sv.found = true;
      sv.post_filled = t.post;
      std::memcpy(sv.rf, t.rf, sizeof(sv.rf));
      std::memcpy(sv.audio, t.audio, sizeof(sv.audio));
      // While this event's clip plays, say where in the event it is. The clip is anchored to its first event only.
      if (s.replay_state == 2) {
        const int i = find_recent(s, s.op_name);
        if (i >= 0 && s.recent[i].seq_count > 0 && s.recent[i].event_seqs[0] == s.scrub_seq) {
          sv.cursor_valid = true;
          sv.cursor_ms = static_cast<int32_t>(now - s.replay_started_ms) - static_cast<int32_t>(s.recent[i].pre_ds) * 100;
        }
      }
    }
  }
  // Audio is only available while demodulated audio is actually flowing (speaker on, or the Audio tab open).
  n.audio_available = s.audio_flowing;
  n.audio_level_dbfs = {};
  n.audio_envelope_valid = s.audio_flowing && s.audio_env_count >= 2;
  if (s.audio_flowing) {
    n.audio_level_dbfs.present = true;
    n.audio_level_dbfs.valid = true;
    n.audio_level_dbfs.value = s.audio_rms;
    n.audio_level_dbfs.baseline_valid = s.audio_detector.baseline_ready();
    n.audio_level_dbfs.baseline = s.audio_detector.baseline();
  }
  n.audio_peak_dbfs = s.audio_peak;
  n.clips_enabled = s.rx.enabled && s.rx.ring.pcm != nullptr;
  n.clip_state = static_cast<uint8_t>(s.rx.planner.state());
  n.clips_saved = s.rx.saved.load(std::memory_order_relaxed);
  n.clips_failed = s.rx.failed.load(std::memory_order_relaxed);
  n.clips_refused = s.rx.refused_no_audio + s.rx.refused_busy + s.rx.refused_limit;
  n.mic_state = s.mic_state;
  n.mic_clip_state = static_cast<uint8_t>(s.mic.planner.state());
  n.mic_clips_saved = s.mic.saved.load(std::memory_order_relaxed);
  n.mic_clips_failed = s.mic.failed.load(std::memory_order_relaxed);
  n.mic_clips_refused = s.mic.refused_no_audio + s.mic.refused_busy + s.mic.refused_limit;
  std::snprintf(n.mic_error, sizeof(n.mic_error), "%s", s.mic_error ? s.mic_error : "");
  for (size_t i = 0; i < kClipInfoCount; ++i) n.clips[i] = s.recent[i];
  n.clip_count = s.recent_count;
  n.selected_clip = std::min<uint8_t>(s.selected, static_cast<uint8_t>(s.recent_count ? s.recent_count - 1 : 0));
  n.verify_state = s.verify_state[n.selected_clip];
  n.replay_state = s.replay_state;
  std::snprintf(n.replay_note, sizeof(n.replay_note), "%s", s.replay_note);
  std::memcpy(n.clip_event_seqs, s.seq_hist, sizeof(n.clip_event_seqs));
  n.clip_event_seq_count = s.seq_hist_count;
  n.audio_baseline_ready = s.audio_detector.baseline_ready();
  n.audio_baseline_dbfs = s.audio_detector.baseline();
  std::memcpy(n.audio_envelope, s.audio_env, sizeof(n.audio_envelope));
  // ENV III: a channel has a value only after a checked reading (CRC / chip id / plausible range); nothing is simulated.
  // Present without a reading yet shows as warming up, and a reading older than the dashboard's limit shows as stale.
  n.environment_sensor_attached = s.env_sensor.sht_present() || s.env_sensor.qmp_present();
  n.temperature_c = n.humidity_rh = n.pressure_hpa = n.motion_g = {};
  n.env_history = &s.env_history;
  // Chart-only changes (a minute average moving) are handed to the dashboard at most every 10 s: a one-minute average
  // gains nothing from faster redraws, and where the body canvas could not be allocated each one is a full repaint.
  if (s.env_history.seq() != s.env_chart_seq && (s.env_chart_seq == 0 || now - s.env_chart_ms >= 10000u)) {
    s.env_chart_seq = s.env_history.seq();
    s.env_chart_ms = now;
  }
  n.env_history_seq = s.env_chart_seq;
  {
    float value = 0;
    uint32_t at = 0;
    n.temperature_c.present = n.humidity_rh.present = s.env_sensor.sht_present();
    n.pressure_hpa.present = s.env_sensor.qmp_present();
    if (s.env_sensor.temperature(&value, &at)) { n.temperature_c.valid = true; n.temperature_c.value = value; n.temperature_c.age_ms = now - at; }
    if (s.env_sensor.humidity(&value, &at)) { n.humidity_rh.valid = true; n.humidity_rh.value = value; n.humidity_rh.age_ms = now - at; }
    if (s.env_sensor.pressure(&value, &at)) { n.pressure_hpa.valid = true; n.pressure_hpa.value = value; n.pressure_hpa.age_ms = now - at; }
  }
  Reading rf{};
  rf.present = ready;
  rf.valid = ready && n.receiver_settled && s.last_level_valid;
  rf.value = s.last_level;
  rf.age_ms = rf.valid ? now - s.last_observe_ms : 0;
  rf.baseline_valid = s.model.baseline_ready();
  rf.baseline = s.model.rf_baseline_dbfs();
  n.rf_level_dbfs = rf;
  const bool spectrum_ok = ready && n.receiver_settled && s.spectrum_have && now - s.spectrum_ms < kSpectrumFreshMs;
  n.spectrum_valid = spectrum_ok;
  n.spectrum_seq = s.spectrum_seq;
  if (spectrum_ok) std::memcpy(n.spectrum_dbfs, s.spectrum, sizeof(n.spectrum_dbfs));
  n.sweeping = s.sweep.running();
  n.sweep_preset = s.sweep_preset;
  n.sweep_dwell_ms = s.sweep_dwell_ms;
  n.sweep_hops = s.sweep.hops();
  n.sweep_unobserved_ms = s.sweep.unobserved_ms();
  n.recorder_state = static_cast<RecorderState>(s.worker.state.load(std::memory_order_acquire));
  n.recorder_session = s.worker.session.load(std::memory_order_relaxed);
  n.recorder_saved = s.worker.saved.load(std::memory_order_relaxed);
  n.recorder_missing = s.worker.missing.load(std::memory_order_relaxed);
  n.recorder_dropped = s.queue.dropped();
}

// Structural changes that need the page recomposed.
// A sensor reading counts as changed when what is drawn changes: its state, going stale (reading_status's 10 s), or the
// value at the one decimal the dashboard prints.
bool reading_shown_differently(const Reading& a, const Reading& b) {
  if (a.present != b.present || a.valid != b.valid || (a.age_ms > 10000) != (b.age_ms > 10000)) return true;
  return a.valid && std::lround(a.value * 10.0f) != std::lround(b.value * 10.0f);
}
bool environment_moved(const Snapshot& a, const Snapshot& b) {
  const Page page = dead_air::page();
  if (page != Page::overview && page != Page::environment) return false;
  return a.environment_sensor_attached != b.environment_sensor_attached ||
         (page == Page::environment && a.env_history_seq != b.env_history_seq) ||
         reading_shown_differently(a.temperature_c, b.temperature_c) ||
         reading_shown_differently(a.humidity_rh, b.humidity_rh) ||
         reading_shown_differently(a.pressure_hpa, b.pressure_hpa);
}

bool visibly_changed(const Snapshot& a, const Snapshot& b) {
  switch (dead_air::overlay()) {  // what an open overlay shows moves on its own clock
    case Overlay::control:
      if (a.control.elapsed_s != b.control.elapsed_s || a.control.observed_s != b.control.observed_s) return true;
      break;
    case Overlay::session:
      if (a.summary.elapsed_s != b.summary.elapsed_s || a.summary.events_rf != b.summary.events_rf ||
          a.summary.events_audio != b.summary.events_audio || a.summary.events_marks != b.summary.events_marks ||
          a.summary.clips != b.summary.clips || a.summary.channels != b.summary.channels) return true;
      break;
    case Overlay::scrub:
      if (a.scrub.found != b.scrub.found || a.scrub.post_filled != b.scrub.post_filled ||
          a.scrub.cursor_valid != b.scrub.cursor_valid || a.scrub.cursor_ms / 100 != b.scrub.cursor_ms / 100) return true;
      break;
    case Overlay::veil:
      if (a.veil.alphabet != b.veil.alphabet || a.veil.live.state != b.veil.live.state ||
          a.veil.event.state != b.veil.event.state || a.veil.enabled != b.veil.enabled || a.veil.logging != b.veil.logging || a.veil.live.valid != b.veil.live.valid ||
          a.veil.live.frame != b.veil.live.frame || a.veil.live.event_sequence != b.veil.live.event_sequence ||
          a.veil.frames != b.veil.frames || a.veil.gaps != b.veil.gaps || a.veil.dropped != b.veil.dropped ||
          a.veil.written != b.veil.written || a.veil.write_failures != b.veil.write_failures ||
          a.veil.skipped / 20u != b.veil.skipped / 20u || a.veil.event.valid != b.veil.event.valid ||
          a.veil.inspect_seq != b.veil.inspect_seq || a.veil.seed != b.veil.seed) return true;
      break;
    default: break;
  }
  return environment_moved(a, b) || a.chime_on != b.chime_on || a.map.visited != b.map.visited || a.map.count != b.map.count ||
         a.control.state != b.control.state || a.control.count[0] != b.control.count[0] ||
         a.control.count[1] != b.control.count[1] || a.control.count[2] != b.control.count[2] ||
         a.receiver_ready != b.receiver_ready || a.receiver_settled != b.receiver_settled ||
         a.frequency_hz != b.frequency_hz || a.sound_enabled != b.sound_enabled ||
         a.battery_percent != b.battery_percent ||
         (a.session_elapsed_s != b.session_elapsed_s && dead_air::page() == Page::overview) ||
         (a.rf_history_seq != b.rf_history_seq && dead_air::page() == Page::overview) ||
         a.recorder_state != b.recorder_state || a.recorder_saved != b.recorder_saved ||
         a.recorder_missing != b.recorder_missing || a.recorder_dropped != b.recorder_dropped ||
         a.recorder_session != b.recorder_session || a.rf_level_dbfs.valid != b.rf_level_dbfs.valid ||
         a.rf_level_dbfs.baseline_valid != b.rf_level_dbfs.baseline_valid ||
         a.audio_available != b.audio_available || a.clips_enabled != b.clips_enabled || a.clip_state != b.clip_state ||
         a.verify_state != b.verify_state || a.selected_clip != b.selected_clip || a.replay_state != b.replay_state || a.clip_event_seq_count != b.clip_event_seq_count ||
         a.clips_saved != b.clips_saved || a.clips_failed != b.clips_failed || a.clips_refused != b.clips_refused ||
         a.mic_state != b.mic_state || a.mic_clip_state != b.mic_clip_state || a.mic_clips_saved != b.mic_clips_saved ||
         a.mic_clips_failed != b.mic_clips_failed || a.mic_clips_refused != b.mic_clips_refused ||
         a.sweeping != b.sweeping || a.sweep_hops != b.sweep_hops || a.sweep_preset != b.sweep_preset ||
         a.sweep_dwell_ms != b.sweep_dwell_ms ||
         a.spectrum_valid != b.spectrum_valid || std::strcmp(a.demod, b.demod) != 0;
}

// Numeric readings shown only on the Overview; they change constantly, so they refresh at most twice a second.
bool levels_moved(const Snapshot& a, const Snapshot& b) {
  if (dead_air::page() != Page::overview) return false;
  const auto q = [](float v) { return static_cast<int>(std::lround(v * 0.5f)); };  // 2 dB steps
  return q(a.rf_level_dbfs.value) != q(b.rf_level_dbfs.value) || q(a.audio_level_dbfs.value) != q(b.audio_level_dbfs.value);
}

}  // namespace

void install(const Hooks& hooks) { g_hooks = hooks; }
bool active() { return g_open && dead_air::active(); }

bool ready_to_open() {
  if (!ready_hooks() || !ensure_state()) return false;
  State& s = *g_state;
  // The previous session's worker frees the audio buffers as its last act: let it finish before they are allocated again.
  const uint32_t begin = millis();
  while (s.worker.alive.load(std::memory_order_acquire) && millis() - begin < kWorkerStopWaitMs) vTaskDelay(pdMS_TO_TICKS(10));
  return !s.worker.alive.load(std::memory_order_acquire);
}

void open() {
  if (!ready_to_open()) return;
  State& s = *g_state;
  const uint32_t now = millis();
  s.model.reset();
  s.model.set_listener(on_event, &s);
  s.opened_ms = now;
  s.last_observe_ms = s.last_snapshot_ms = 0;
  s.settle_until_ms = now + kSettleMs;
  s.have_signature = false;
  s.last_level_valid = false;
  s.spectrum_have = false;
  s.last_total = 0;
  s.snapshot = Snapshot();
  s.audio.meter.configure(48000);
  (void)alloc_ring(s.rx.ring);  // the receiver clip ring: 12 s of demodulated PCM in PSRAM, allocated once
  s.rx.ring.written.store(0, std::memory_order_relaxed);
  s.rx.ring.run_start.store(0, std::memory_order_relaxed);
  s.rx.ring.last_write_ms.store(0, std::memory_order_relaxed);
  s.rx.ring.capture.store(s.rx.enabled && s.rx.ring.pcm != nullptr, std::memory_order_relaxed);
  s.rx.planner.configure(kClipSampleRate, kClipPreMs, kClipPostMs);
  s.rx.mail.state.store(0, std::memory_order_relaxed);
  s.rx.saved.store(0, std::memory_order_relaxed);
  s.rx.failed.store(0, std::memory_order_relaxed);
  s.rx.refused_no_audio = s.rx.refused_busy = s.rx.refused_limit = 0;
  // The microphone starts every session OFF; only an explicit tap turns it on.
  s.mic.enabled = false;
  s.mic_resume = false;  // a replay from the last session must not switch the mic on in this one
  s.mic_resume_due_ms = 0;
  s.mic.saved.store(0, std::memory_order_relaxed);
  s.mic.failed.store(0, std::memory_order_relaxed);
  s.mic.refused_no_audio = s.mic.refused_busy = s.mic.refused_limit = 0;
  s.mic.mail.state.store(0, std::memory_order_relaxed);
  s.mic_state = 0;
  s.mic_error = nullptr;
  s.mic_used = false;
  s.recent_count = 0;
  std::memset(s.verify_state, 0, sizeof(s.verify_state));
  s.selected = 0;
  s.pinned = false;
  s.replay_state = 0;
  s.replay_note = "";
  s.log_pending = false;
  g_replaying.store(false, std::memory_order_relaxed);
  s.seq_hist_count = 0;
  s.audio.head.store(0, std::memory_order_relaxed);
  s.audio_tail = 0;
  s.audio_detector.reset();
  s.audio_env_count = 0;
  s.audio_flowing = false;
  s.last_audio_ms = 0;
  s.last_level_refresh_ms = 0;
  std::memset(s.hist, 0, sizeof(s.hist));
  std::memset(s.hist_mark, 0, sizeof(s.hist_mark));
  s.hist_count = 0;
  s.hist_ms = s.hist_seq = s.hist_total = 0;
  for (EventTrace& t : s.traces) t.seq = 0;
  for (size_t i = 0; i < kTracePre; ++i) s.fine_rf[i] = s.fine_audio[i] = kHistGap;
  s.fine_ms = 0;
  s.scrub_seq = 0;
  s.control.reset();
  s.control_was_running = false;
  s.stats.reset(now);
  env::Bus* port_a = env::port_a_bus();
  s.env_bus = port_a != nullptr;
  s.env_sensor.begin(port_a);  // forgets any earlier readings; the module is probed again every time the screen opens
  s.env_events.reset();
  s.env_history.reset(now);
  s.env_chart_seq = 0;
  s.env_chart_ms = 0;
  for (int c = 0; c < 3; ++c) { s.env_last_at[c] = 0; s.env_seen[c] = false; }
  s.map = BandMapView{};
  s.chime_on = false;  // the chime is off at every session start; only a tap turns it on
  s.chime_active = false;
  s.chime_last_ms = 0;
  s.veil_on = false;   // VEIL-26 is opt-in and off at every session start, like the chime and the microphone
  s.veil_alphabet = runes::Alphabet::latin;
  s.veil_intent = 0;
  s.veil_inspect_seq = 0;
  s.veil_cfg_pending = false;
  veil_restart(s, kVeilIntentions[0]);
  (void)start_worker();
  if (g_hooks.set_demod) g_hooks.set_demod(0);  // NFM, the browse band's own demodulator
  build_snapshot(now);
  s.snapshot.revision = 1;
  s.pushed = s.snapshot;
  screens::begin_transition(screens::Id::dead_air, now);
  dead_air::attach_events(&s.model);
  dead_air::enter(s.snapshot);
  screens::finish_transition();
  g_ring.store(&s.audio, std::memory_order_release);  // the DSP thread may now offer audio
  g_rx_ring.store(&s.rx.ring, std::memory_order_release);
  g_open = true;
}

void close() {
  if (!g_open || !g_state) return;
  g_open = false;
  g_ring.store(nullptr, std::memory_order_release);
  g_rx_ring.store(nullptr, std::memory_order_release);
  if (g_state->replay_state == 2 && g_hooks.replay_stop) g_hooks.replay_stop();  // never keep playing after the screen is gone
  g_replaying.store(false, std::memory_order_release);
  g_state->replay_state = 0;
  g_state->rx.ring.capture.store(false, std::memory_order_relaxed);
  abandon_capture(g_state->rx, "session_closed");  // the worker records why this clip never happened
  mic_stop(*g_state, "session_closed");            // the microphone never outlives the screen
  g_audio_wanted.store(false, std::memory_order_relaxed);
  stop_sweep(*g_state, millis());  // logs the stop and puts the receiver back before the journal closes
  end_chime(*g_state);
  if (g_state->control.running()) {  // a run cut short by leaving still records what it measured
    g_state->control.stop(millis());
    g_state->control_was_running = false;
    post_control_note(*g_state);
  }
  post_session_note(*g_state, millis());  // the worker writes it before it closes the file
  veil_post_summary(*g_state);
  dead_air::leave();
  g_state->model.set_listener(nullptr, nullptr);
  if (g_hooks.release_demod) g_hooks.release_demod();
  g_state->worker.stop.store(true, std::memory_order_release);  // worker flushes, syncs and closes the file
}

// Each accepted sensor sample is fed to the environment detector once. A channel that has no reading (module unplugged,
// dropped after repeated failures) is reset, so an unplug can never look like a departure from the baseline.
void feed_env_events(State& s, uint32_t now) {
  s.env_history.tick(now);  // a silent sensor must show as gaps, not as its last value
  const EnvChannel channels[3] = {EnvChannel::temperature, EnvChannel::humidity, EnvChannel::pressure};
  for (int c = 0; c < 3; ++c) {
    float value = 0.0f;
    uint32_t at = 0;
    const bool have = c == 0   ? s.env_sensor.temperature(&value, &at)
                      : c == 1 ? s.env_sensor.humidity(&value, &at)
                               : s.env_sensor.pressure(&value, &at);
    if (!have) {
      s.env_events.reset(channels[c]);
      s.env_seen[c] = false;
      continue;
    }
    if (s.env_seen[c] && at == s.env_last_at[c]) continue;
    s.env_seen[c] = true;
    s.env_last_at[c] = at;
    s.env_history.add(static_cast<size_t>(c), value, at);
    EnvFinding f;
    if (s.env_events.observe(channels[c], value, at, s.model.sensitivity(), &f))
      (void)s.model.append_measured(env_event(f, now, g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0, s.model.sensitivity()));
  }
}

void tick() {
  if (!g_open || !g_state || !dead_air::active()) return;
  State& s = *g_state;
  const uint32_t now = millis();
  s.env_sensor.service(now);  // a few short I2C transfers once a second; nothing in between
  feed_env_events(s, now);
  if (!s.rx.ring.pcm && now - s.ring_retry_ms >= 3000) {
    // open() asked for the clip ring while another screen's buffers (or a retune still in flight) may have refused to
    // release; once they can, clip capture starts without the owner doing anything.
    s.ring_retry_ms = now;
    if (alloc_ring(s.rx.ring)) s.rx.ring.capture.store(s.rx.enabled, std::memory_order_release);
  }
  const bool ready = g_hooks.receiver_ready();
  const uint32_t signature = g_hooks.config_signature();
  s.rx_settled = ready && static_cast<int32_t>(now - s.settle_until_ms) >= 0;
  if (!s.have_signature || signature != s.last_signature || !ready) {
    // A retune, gain/rate change or receiver (re)start invalidates the baseline and the settling window.
    if (!s.have_signature || signature != s.last_signature || s.last_level_valid) {
      ++s.config_revision;
      if (s.rx.ring.pcm) s.rx.ring.run_start.store(s.rx.ring.written.load(std::memory_order_relaxed), std::memory_order_relaxed);
      s.settle_until_ms = now + (s.sweep.running() ? kSweepSettleMs : kSettleMs);
    }
    s.last_signature = signature;
    s.have_signature = true;
    s.last_level_valid = false;
  }
  if (s.sweep.running()) {
    // A hop only counts as settled once the receiver reports the commanded channel and the assumed settle has passed.
    const bool settled = ready && g_hooks.frequency_hz() == s.sweep.current_hz() &&
                         static_cast<int32_t>(now - s.settle_until_ms) >= 0;
    uint32_t hz = 0;
    if (s.sweep.tick(now, ready, settled, &hz)) {
      // A refused hop is abandoned by the sweep's own stall timer and counted there.
      if (g_hooks.hot_retune) (void)g_hooks.hot_retune(hz);
      else if (g_hooks.tune) g_hooks.tune(hz);
      s.settle_until_ms = now + kSweepSettleMs;
    }
  }
  if (now - s.last_observe_ms >= kObserveIntervalMs) {
    RfObservation obs{};
    obs.monotonic_ms = now;
    obs.unix_time = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0;
    obs.frequency_hz = ready ? g_hooks.frequency_hz() : 0;
    obs.configuration_revision = s.config_revision;
    // While a sweep runs the detector is suspended: every hop is a new channel, so there is no stable baseline.
    obs.settled = ready && !s.sweep.running() && static_cast<int32_t>(now - s.settle_until_ms) >= 0;
    const uint32_t counter = g_hooks.rf_sample_counter();
    float level = 0.0f;
    const bool fresh = counter != s.last_counter;
    s.last_counter = counter;
    obs.valid = ready && fresh && g_hooks.rf_level_dbfs(&level) && std::isfinite(level);
    obs.level_dbfs = level;
    s.last_observe_ms = now;
    if (obs.valid) { s.last_level = level; s.last_level_valid = obs.settled; }
    else if (!fresh && ready) { /* between blocks: keep the last settled level */ }
    else s.last_level_valid = false;
    if (s.control.running()) {
      // A control run feeds the same sample to three detectors and pauses the live one, so its counts never mix with findings.
      s.control.observe(obs);
      RfObservation paused = obs;
      paused.valid = false;
      s.model.observe_rf(paused);
    } else {
      s.model.observe_rf(obs);
    }
    veil_observe(s, obs, now);  // after the detector has seen the sample; off unless the owner switched VEIL-26 on
    s.stats.on_level(now, obs.valid && obs.settled, level);
    if (ready) s.stats.on_frequency(g_hooks.frequency_hz());
    if (s.sweep.running() && obs.valid && ready && g_hooks.frequency_hz() == s.sweep.current_hz() &&
        static_cast<int32_t>(now - s.settle_until_ms) >= 0 && std::isfinite(level)) {
      record_map_level(s, s.sweep.current_hz(), level);  // the level while the receiver dwells on this channel
    }
  }
  s.control.tick(now);
  if (s.control_was_running && !s.control.running()) {  // the run just ended (time up or stopped): record the result
    s.control_was_running = false;
    post_control_note(s);
  }
  update_history(s, now);
  // Audio level buckets from the DSP thread: update the envelope and run the activity detector.
  bool audio_changed = false;
  float fresh_bars[8];
  size_t fresh_count = 0;
  const bool on_audio_tab = screens::owns(screens::Id::dead_air) && dead_air::page() == Page::audio;
  g_audio_wanted.store(on_audio_tab || (s.rx.enabled && s.rx.ring.pcm && ready), std::memory_order_relaxed);
  if (dead_air::page() != s.last_page) {
    s.last_page = dead_air::page();
    if (on_audio_tab) audio_changed = true;  // paint the live part of the tab right after it opens
  }
  {
    const uint32_t head = s.audio.head.load(std::memory_order_acquire);
    if (head - s.audio_tail > AudioRing::kSize) {  // the UI fell behind: drop the oldest and restart the detector
      s.audio_tail = head - AudioRing::kSize;
      s.audio_detector.reset();
    }
    while (s.audio_tail != head) {
      const AudioBucket b = s.audio.slots[s.audio_tail % AudioRing::kSize];
      ++s.audio_tail;
      s.audio_rms = b.rms_dbfs;
      s.audio_peak = b.peak_dbfs;
      s.last_audio_ms = now;
      const float norm = std::clamp((b.peak_dbfs + 70.0f) / 70.0f, 0.0f, 1.0f);
      std::memmove(s.audio_env, s.audio_env + 1, (kBins - 1) * sizeof(float));
      s.audio_env[kBins - 1] = norm;
      s.audio_env_count = std::min<size_t>(s.audio_env_count + 1, kBins);
      if (fresh_count == 8) { std::memmove(fresh_bars, fresh_bars + 1, 7 * sizeof(float)); --fresh_count; }
      fresh_bars[fresh_count++] = norm;
      audio_changed = true;
      AudioFinding f;
      // Not while a sweep is hopping: every hop is a different channel.
      if (s.rx_settled && !s.sweep.running() && !s.control.running() &&
          s.audio_detector.observe(b, now, s.config_revision, s.model.sensitivity(), &f)) {
        Event e{};
        e.monotonic_ms = now;
        e.unix_time = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0;
        e.frequency_hz = ready ? g_hooks.frequency_hz() : 0;
        e.source = Source::audio;
        e.kind = Kind::audio_burst;
        e.value = f.rms_dbfs;
        e.reference = f.baseline_dbfs;
        e.threshold = f.threshold_db;
        (void)s.model.append_measured(e);
      } else if (!s.rx_settled || s.sweep.running() || s.control.running()) {
        s.audio_detector.reset();
      }
    }
    const bool flowing = s.last_audio_ms != 0 && now - s.last_audio_ms < 500;
    if (flowing != s.audio_flowing) {
      s.audio_flowing = flowing;
      audio_changed = true;
      if (!flowing) s.audio_detector.reset();
    }
  }
  s.rx.flowing = s.audio_flowing;
  if (s.mic.enabled) {
    const bool speaker_on = (g_hooks.sound_enabled && g_hooks.sound_enabled()) || g_replaying.load(std::memory_order_relaxed);
    const uint32_t last = s.mic.ring.last_write_ms.load(std::memory_order_relaxed);
    s.mic.flowing = !speaker_on && last != 0 && now - last < 500;
    if (!dead_air_mic::running()) { s.mic_error = "mic stopped"; mic_stop(s, "mic_failed"); s.mic_state = 3; }
    else s.mic_state = speaker_on ? 2 : 1;  // paused while the speaker is audible, otherwise listening
    if (s.mic.enabled && !speaker_on) {
      if (s.mic_restart_due_ms && static_cast<int32_t>(now - s.mic_restart_due_ms) >= 0) {
        s.mic_restart_due_ms = 0;
        mic_restart(s, "mic_restarted");
      } else if (dead_air_mic::flat_blocks() >= 50 && now - s.mic_last_restart_ms > 3000) {  // 1 s of one constant value
        if (++s.mic_restarts > 3) {
          s.mic_error = "mic silent";
          mic_stop(s, "mic_silent");
          s.mic_state = 3;
        } else {
          mic_restart(s, "mic_stalled");
        }
      } else if (dead_air_mic::flat_blocks() == 0 && now - s.mic_last_restart_ms > 5000) {
        s.mic_restarts = 0;
      }
    }
  }
  if (s.mic_resume && s.replay_state != 1 && s.replay_state != 2 && !g_replaying.load(std::memory_order_relaxed) &&
      (!s.mic_resume_due_ms || static_cast<int32_t>(now - s.mic_resume_due_ms) >= 0)) {
    s.mic_resume = false;
    s.mic_resume_due_ms = 0;
    (void)mic_start(s);
  }
  if (s.replay_state == 2 && g_hooks.replay_playing && now - s.replay_started_ms > 300 && !g_hooks.replay_playing()) end_replay(s, true);
  if (s.chime_active && now - s.chime_start_ms > 300 && g_hooks.replay_playing && !g_hooks.replay_playing()) end_chime(s);
  {
    // The orb: its idle glow follows how close the live level is to the event limit; events burst it (see update_history).
    const bool live = ready && s.rx_settled && s.last_level_valid;
    float proximity = 0.0f;
    if (live && s.model.baseline_ready()) {
      proximity = std::clamp((s.last_level - s.model.rf_baseline_dbfs()) / rf_rise_threshold_db(s.model.sensitivity()), 0.0f, 1.0f);
    } else if (live) {
      proximity = 0.1f;  // still learning the baseline
    }
    dead_air::fx_idle(live, proximity);
    (void)dead_air::draw_fx(now);
  }
  if (s.log_pending && s.rx.mail.state.load(std::memory_order_acquire) == 0) {
    ClipTicket t{};
    t.log_play = true;
    t.played_to_end = s.log_completed;
    std::snprintf(t.verify_name, sizeof(t.verify_name), "%s", s.log_name);
    s.log_pending = false;
    post_ticket(s.rx, t);
  }
  service_clips(s, s.rx, now);
  service_clips(s, s.mic, now);
  if (on_audio_tab && audio_changed) {
    AudioView v{};
    v.envelope = s.audio_env;
    v.count = s.audio_env_count;
    v.fresh = fresh_bars;
    v.fresh_count = fresh_count;
    v.flowing = s.audio_flowing;
    v.rms_dbfs = s.audio_rms;
    v.peak_dbfs = s.audio_peak;
    v.baseline_ready = s.audio_detector.baseline_ready();
    v.baseline_dbfs = s.audio_detector.baseline();
    (void)dead_air::draw_audio_frame(v, now);
  }
  if (!screens::owns(screens::Id::dead_air)) return;
  if (now - s.last_snapshot_ms < 100) return;
  const uint32_t revision = s.snapshot.revision;
  build_snapshot(now);
  const bool events_changed = s.model.total() != s.last_total;
  const bool slow_due = levels_moved(s.snapshot, s.pushed) && now - s.last_level_refresh_ms >= 500;
  const bool changed = visibly_changed(s.snapshot, s.pushed) || events_changed || slow_due;
  if (changed) {
    s.last_total = s.model.total();
    s.snapshot.revision = revision + 1;
    s.pushed = s.snapshot;
    s.last_snapshot_ms = now;
    if (slow_due) s.last_level_refresh_ms = now;
    screens::note_visible_update(screens::Id::dead_air);
    dead_air::update(s.snapshot);
  } else {
    s.snapshot.revision = revision;
    s.last_snapshot_ms = now;
  }
}

void draw(bool static_panel) {
  if (!g_open || !g_state) return;
  if (!static_panel && !screens::may_draw(screens::Id::dead_air)) return;
  if (static_panel) {
    build_snapshot(millis());
    dead_air::enter(g_state->snapshot);
  }
}

void touch(int32_t x, int32_t y) {
  if (!g_open || !g_state) return;
  const Action action = dead_air::handle_touch(x, y, millis());
  ++g_touches;
  if (action.kind != ActionKind::none) g_last_action = static_cast<uint8_t>(action.kind);
  switch (action.kind) {
    case ActionKind::close: if (g_hooks.go_home) g_hooks.go_home(); break;
    case ActionKind::toggle_mute: if (g_hooks.toggle_mute) g_hooks.toggle_mute(); break;
    case ActionKind::open_settings: if (g_hooks.open_settings) g_hooks.open_settings(); break;
    case ActionKind::verify_clip: request_verify(*g_state); break;
    case ActionKind::select_clip:
      if (action.value >= 0 && action.value < g_state->recent_count) {
        g_state->selected = static_cast<uint8_t>(action.value);
        g_state->pinned = action.value != 0;
      }
      break;
    case ActionKind::play_clip: request_play(*g_state); break;
    case ActionKind::stop_replay: end_replay(*g_state, false); break;
    case ActionKind::toggle_clips:
      g_state->rx.enabled = !g_state->rx.enabled;
      g_state->rx.ring.capture.store(g_state->rx.enabled && g_state->rx.ring.pcm != nullptr, std::memory_order_relaxed);
      break;
    case ActionKind::toggle_mic:
      if (g_state->mic.enabled) mic_stop(*g_state, "mic_stopped");
      else (void)mic_start(*g_state);
      break;
    case ActionKind::start_scan: start_sweep(*g_state, millis()); break;
    case ActionKind::stop_scan: stop_sweep(*g_state, millis()); break;
    case ActionKind::sweep_preset:
      if (!g_state->sweep.running()) g_state->sweep_preset = (g_state->sweep_preset + 1) % kSweepPresetCount;
      break;
    case ActionKind::sweep_dwell:
      if (!g_state->sweep.running()) {
        uint8_t i = 0;
        while (i < kSweepDwellCount && kSweepDwellChoicesMs[i] != g_state->sweep_dwell_ms) ++i;
        g_state->sweep_dwell_ms = kSweepDwellChoicesMs[(i + 1) % kSweepDwellCount];
      }
      break;
    case ActionKind::set_demod:
      if (g_state->sweep.running()) break;  // the schedule owns tuning while it runs
      if (g_hooks.set_demod && g_hooks.receiver_ready() && action.value >= 0 && action.value < kModeCount) {
        g_hooks.set_demod(static_cast<uint8_t>(action.value));
        if (g_hooks.tune) g_hooks.tune(g_hooks.frequency_hz());  // restart the stream cleanly in the new mode
      }
      break;
    case ActionKind::tune_to:
      if (g_state->sweep.running()) break;
      if (g_hooks.tune && g_hooks.receiver_ready() && action.value > 0) g_hooks.tune(static_cast<uint32_t>(action.value));
      break;
    case ActionKind::scrub_open: g_state->scrub_seq = static_cast<uint32_t>(action.value); break;
    case ActionKind::scrub_close: g_state->scrub_seq = 0; break;
    case ActionKind::play_event_clip: {
      // The clip that covers the event (a radio clip is preferred to a microphone one): choose it, then play it as usual,
      // which re-checks its hash first.
      State& s = *g_state;
      int pick = -1;
      for (uint8_t i = 0; i < s.recent_count && pick < 0; ++i) {
        for (uint8_t k = 0; k < s.recent[i].seq_count; ++k)
          if (s.recent[i].event_seqs[k] == static_cast<uint32_t>(action.value) && !s.recent[i].mic) { pick = i; break; }
      }
      for (uint8_t i = 0; i < s.recent_count && pick < 0; ++i) {
        for (uint8_t k = 0; k < s.recent[i].seq_count; ++k)
          if (s.recent[i].event_seqs[k] == static_cast<uint32_t>(action.value)) { pick = i; break; }
      }
      if (pick >= 0) {
        s.selected = static_cast<uint8_t>(pick);
        s.pinned = pick != 0;
        request_play(s);
      }
      break;
    }
    case ActionKind::control_start:
      if (!g_state->control.running() && !g_state->sweep.running() && g_hooks.receiver_ready()) {
        g_state->control.start(millis());
        g_state->control_was_running = true;
      }
      break;
    case ActionKind::control_stop: g_state->control.stop(millis()); break;  // tick() records the result
    case ActionKind::veil_open: g_state->veil_inspect_seq = static_cast<uint32_t>(action.value); break;
    case ActionKind::veil_close: g_state->veil_inspect_seq = 0; break;
    case ActionKind::veil_toggle: {
      State& s = *g_state;
      if (s.veil_on) {
        veil_post_summary(s);  // the file says how this stretch of analysis ended
        s.veil_on = false;
      } else {
        s.veil_on = true;
        veil_restart(s, s.veil.intention());
      }
      break;
    }
    case ActionKind::veil_alphabet:
      runes::set_alphabet(g_state->veil_alphabet == runes::Alphabet::latin ? runes::Alphabet::elder : runes::Alphabet::latin,
                          g_state->veil_alphabet, g_state->veil_on, g_state->veil_cfg_pending);
      break;
    case ActionKind::veil_intention: {
      State& s = *g_state;
      s.veil_intent = s.veil_intent == kVeilCustom ? 0 : static_cast<uint8_t>((s.veil_intent + 1) % kVeilIntentionCount);
      veil_restart(s, kVeilIntentions[s.veil_intent]);
      break;
    }
    case ActionKind::toggle_chime:
      if (g_state->chime_on) { g_state->chime_on = false; end_chime(*g_state); }
      else g_state->chime_on = build_chime(*g_state);
      break;
    case ActionKind::tune_up:
    case ActionKind::tune_down:
      if (g_state->sweep.running()) break;
      if (g_hooks.tune && g_hooks.step_frequency && g_hooks.receiver_ready()) {
        g_hooks.tune(g_hooks.step_frequency(g_hooks.frequency_hz(), action.value > 0 ? 1 : -1));
      }
      break;
    default: break;  // start/stop recording, save clip and export have no buttons yet
  }
}

void offer_audio(const int16_t* pcm, size_t count) {
  AudioRing* r = g_ring.load(std::memory_order_acquire);
  if (!r || !pcm || count == 0) return;
  g_rx_writers.fetch_add(1, std::memory_order_acq_rel);  // announced before the ring pointer is read: release_audio_buffers waits for 0
  if (PcmRing* ring = g_rx_ring.load(std::memory_order_acquire)) ring_write(*ring, pcm, count);
  g_rx_writers.fetch_sub(1, std::memory_order_acq_rel);
  AudioBucket out[4];
  const size_t m = r->meter.feed(pcm, count, out, 4);
  for (size_t i = 0; i < m; ++i) {
    const uint32_t head = r->head.load(std::memory_order_relaxed);
    r->slots[head % AudioRing::kSize] = out[i];
    r->head.store(head + 1, std::memory_order_release);
  }
}

bool wants_audio() { return g_audio_wanted.load(std::memory_order_relaxed); }

bool wants_spectrum() {
  return active() && screens::owns(screens::Id::dead_air) && dead_air::page() == Page::spectrum;
}

void feed_spectrum(const float* levels, size_t bins, uint32_t sample_rate_sps) {
  (void)sample_rate_sps;
  if (!g_open || !g_state || !levels || bins < kBins) return;
  const float offset_db = 20.0f * std::log10(kFullScaleAmplitude * (static_cast<float>(bins) - 1.0f) * 0.5f);
  State& s = *g_state;
  // Max-pool the receiver's FFT to the trace resolution, then derive the coarse bins from those points.
  float points[kTracePoints];
  for (size_t i = 0; i < kTracePoints; ++i) {
    const size_t a = i * bins / kTracePoints, b = std::max(a + 1, (i + 1) * bins / kTracePoints);
    float peak = -200.0f;
    for (size_t k = a; k < b && k < bins; ++k) peak = std::max(peak, levels[k]);
    points[i] = peak - offset_db;  // per-bin power, relative dBFS
  }
  for (size_t i = 0; i < kBins; ++i) {
    const size_t a = i * kTracePoints / kBins, b = std::max(a + 1, (i + 1) * kTracePoints / kBins);
    float peak = -200.0f;
    for (size_t k = a; k < b && k < kTracePoints; ++k) peak = std::max(peak, points[k]);
    s.spectrum[i] = peak;
  }
  s.spectrum_ms = millis();
  s.spectrum_have = true;
  ++s.spectrum_seq;
  // Only a settled receiver is drawn, and never under the volume tray.
  if (s.rx_settled && !(g_hooks.overlay_open && g_hooks.overlay_open()))
    (void)dead_air::draw_spectrum_frame(points, kTracePoints, s.config_revision, s.spectrum_ms);
}

uint32_t flood_manual_marks(uint32_t count) {
  if (!g_open || !g_state) return 0;
  count = std::min<uint32_t>(count, 500);
  const uint32_t now = millis();
  const uint32_t unix_time = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0;
  const uint32_t hz = g_hooks.frequency_hz ? g_hooks.frequency_hz() : 0;
  for (uint32_t i = 0; i < count; ++i) g_state->model.mark_manual(now, unix_time, hz);
  return count;
}

void describe(char* out, size_t size) {
  if (!out || !size) return;
  if (!g_state) { std::snprintf(out, size, "DEAD_AIR open=0"); return; }
  const Worker& w = g_state->worker;
  const PaintStats ps = dead_air::paint_stats();
  std::snprintf(out, size,
                "DEAD_AIR open=%d recorder=%u session=%lu saved=%lu missing=%lu dropped=%lu events=%lu baseline=%d level=%.1f base_dbfs=%.1f thr_db=%.0f hz=%lu mode=%s sweep=%d hops=%lu stalls=%lu unobs_ms=%lu touches=%lu last_action=%u resume=%lu spec_seq=%lu spec_valid=%d paint_us=%lu/%lu/%lu max_push_us=%lu px=%lu rects=%lu whole=%lu part=%lu frame_us=%lu max_frame_us=%lu frames=%lu audio=%d rms=%.1f peak=%.1f snd=%d abase=%d clip=%u cs=%lu cf=%lu cr=%lu mic=%u ms=%lu mf=%lu mr=%lu mic_blocks=%lu rp=%u sel=%u rn=\"%s\"",
                g_open ? 1 : 0, static_cast<unsigned>(w.state.load()), static_cast<unsigned long>(w.session.load()),
                static_cast<unsigned long>(w.saved.load()), static_cast<unsigned long>(w.missing.load()),
                static_cast<unsigned long>(w.dropped.load()), static_cast<unsigned long>(g_state->model.total()),
                g_state->model.baseline_ready() ? 1 : 0, g_state->last_level_valid ? static_cast<double>(g_state->last_level) : -999.0,
                (g_state->model.baseline_ready() ? static_cast<double>(g_state->model.rf_baseline_dbfs()) : -999.0), static_cast<double>(rf_rise_threshold_db(g_state->model.sensitivity())),
                static_cast<unsigned long>(g_hooks.frequency_hz ? g_hooks.frequency_hz() : 0),
                g_hooks.demod && g_hooks.receiver_ready && g_hooks.receiver_ready() ? g_hooks.demod() : "--",
                g_state->sweep.running() ? 1 : 0, static_cast<unsigned long>(g_state->sweep.hops()),
                static_cast<unsigned long>(g_state->sweep.stalls()), static_cast<unsigned long>(g_state->sweep.unobserved_ms()),
                static_cast<unsigned long>(g_touches), static_cast<unsigned>(g_last_action), static_cast<unsigned long>(g_state->sweep.resume_hz()),
                static_cast<unsigned long>(g_state->spectrum_seq), g_state->snapshot.spectrum_valid ? 1 : 0,
                static_cast<unsigned long>(ps.compose_us), static_cast<unsigned long>(ps.diff_us),
                static_cast<unsigned long>(ps.push_us), static_cast<unsigned long>(ps.push_us_max),
                static_cast<unsigned long>(ps.pixels), static_cast<unsigned long>(ps.rects),
                static_cast<unsigned long>(ps.whole_pushes), static_cast<unsigned long>(ps.partial_pushes),
                static_cast<unsigned long>(ps.frame_us), static_cast<unsigned long>(ps.frame_us_max),
                static_cast<unsigned long>(ps.frames),
                g_state->audio_flowing ? 1 : 0, static_cast<double>(g_state->audio_rms), static_cast<double>(g_state->audio_peak),
                (g_hooks.sound_enabled && g_hooks.sound_enabled()) ? 1 : 0,
                g_state->audio_detector.baseline_ready() ? 1 : 0,
                static_cast<unsigned>(g_state->rx.planner.state()), static_cast<unsigned long>(g_state->rx.saved.load()),
                static_cast<unsigned long>(g_state->rx.failed.load()),
                static_cast<unsigned long>(g_state->rx.refused_no_audio + g_state->rx.refused_busy),
                static_cast<unsigned>(g_state->mic_state), static_cast<unsigned long>(g_state->mic.saved.load()),
                static_cast<unsigned long>(g_state->mic.failed.load()),
                static_cast<unsigned long>(g_state->mic.refused_no_audio + g_state->mic.refused_busy + g_state->mic.refused_limit),
                static_cast<unsigned long>(dead_air_mic::stats().blocks), static_cast<unsigned>(g_state->replay_state),
                static_cast<unsigned>(g_state->selected), g_state->replay_note);
}

void describe_env(char* out, size_t size) {
  if (!out || !size) return;
  if (!g_state || !g_open) { std::snprintf(out, size, "DEAD_AIR_ENV open=0 (the sensor is only polled while Dead Air is open)"); return; }
  const env::EnvSensor& e = g_state->env_sensor;
  const uint32_t now = millis();
  float t = 0, rh = 0, p = 0;
  uint32_t t_at = 0, rh_at = 0, p_at = 0;
  const bool have_t = e.temperature(&t, &t_at), have_rh = e.humidity(&rh, &rh_at), have_p = e.pressure(&p, &p_at);
  const env::Counters& c = e.counters();
  std::snprintf(out, size,
                "DEAD_AIR_ENV open=1 bus=%d sht=%d qmp=%d temp_c=%.2f temp_age_ms=%ld rh=%.2f rh_age_ms=%ld hpa=%.2f hpa_age_ms=%ld "
                "sht_ok=%lu sht_crc=%lu sht_bus=%lu qmp_ok=%lu qmp_bad=%lu qmp_bus=%lu attaches=%lu",
                g_state->env_bus ? 1 : 0, e.sht_present() ? 1 : 0, e.qmp_present() ? 1 : 0,
                have_t ? static_cast<double>(t) : -999.0, have_t ? static_cast<long>(now - t_at) : -1L,
                have_rh ? static_cast<double>(rh) : -999.0, have_rh ? static_cast<long>(now - rh_at) : -1L,
                have_p ? static_cast<double>(p) : -999.0, have_p ? static_cast<long>(now - p_at) : -1L,
                static_cast<unsigned long>(c.sht_ok), static_cast<unsigned long>(c.sht_crc), static_cast<unsigned long>(c.sht_bus),
                static_cast<unsigned long>(c.qmp_ok), static_cast<unsigned long>(c.qmp_bad), static_cast<unsigned long>(c.qmp_bus),
                static_cast<unsigned long>(c.attaches));
}

void describe_veil(char* out, size_t size) {
  if (!out || !size) return;
  if (!g_state || !g_open) { std::snprintf(out, size, "DEAD_AIR_VEIL open=0 (VEIL-26 only runs while Dead Air is open)"); return; }
  const State& s = *g_state;
  const veil26::Counters c = veil_counters(s);
  std::snprintf(out, size,
                "DEAD_AIR_VEIL open=1 on=%d logging=%d dict=%s alphabet=%s intention=\"%s\" seed=%08lx frames=%lu gaps=%lu skipped=%lu "
                "posted=%lu dropped=%lu written=%lu write_failures=%lu pending=%lu state=%08lx letters=\"%s\" "
                "live_hz=%lu live_dbfs=%.1f live_word=%s",
                s.veil_on ? 1 : 0, s.veil_logging.load(std::memory_order_relaxed) ? 1 : 0, veil26::kDictionaryId, runes::alphabet(s.veil_alphabet).id, s.veil.intention(),
                static_cast<unsigned long>(s.veil.seed()), static_cast<unsigned long>(c.frames), static_cast<unsigned long>(c.gaps),
                static_cast<unsigned long>(c.skipped_invalid), static_cast<unsigned long>(c.records_posted),
                static_cast<unsigned long>(c.records_dropped), static_cast<unsigned long>(c.records_written),
                static_cast<unsigned long>(c.write_failures), static_cast<unsigned long>(s.veil_queue.pending()),
                static_cast<unsigned long>(s.veil.state()), s.veil.letters(),
                static_cast<unsigned long>(s.veil_live.frequency_hz), static_cast<double>(s.veil_live.level_dbfs),
                s.veil_live.valid ? s.veil_live.word : "-");
}

bool veil_command(const char* args, char* reply, size_t size) {
  if (!reply || !size) return false;
  reply[0] = '\0';
  if (!args || !g_state || !g_open) { std::snprintf(reply, size, "RTL_DEAD_AIR_VEIL_ERROR not_open"); return false; }
  State& s = *g_state;
  if (std::strcmp(args, "ON") == 0) {
    if (!s.veil_on) { s.veil_on = true; veil_restart(s, s.veil.intention()); }
  } else if (std::strcmp(args, "OFF") == 0) {
    if (s.veil_on) { veil_post_summary(s); s.veil_on = false; }
  } else if (std::strncmp(args, "ALPHABET ", 9) == 0) {
    runes::Alphabet alphabet;
    if (!runes::parse(args + 9, &alphabet)) {
      std::snprintf(reply, size, "RTL_DEAD_AIR_VEIL_ERROR use ALPHABET LATIN|ELDER");
      return false;
    }
    runes::set_alphabet(alphabet, s.veil_alphabet, s.veil_on, s.veil_cfg_pending);
  } else if (std::strncmp(args, "INTENT ", 7) == 0) {
    char text[veil26::kMaxIntention + 1] = {};
    size_t n = 0;
    for (const char* p = args + 7; *p && n < veil26::kMaxIntention; ++p)
      if (*p >= 0x20 && *p <= 0x7e) text[n++] = *p;  // printable ASCII only; the file and the screen stay plain text
    if (n == 0) { std::snprintf(reply, size, "RTL_DEAD_AIR_VEIL_ERROR empty_intention"); return false; }
    s.veil_intent = kVeilCustom;
    veil_restart(s, text);
  } else {
    std::snprintf(reply, size, "RTL_DEAD_AIR_VEIL_ERROR use ON, OFF, INTENT <text> or ALPHABET LATIN|ELDER");
    return false;
  }
  std::snprintf(reply, size, "RTL_DEAD_AIR_VEIL_OK on=%d alphabet=%s intention=\"%s\"", s.veil_on ? 1 : 0, runes::alphabet(s.veil_alphabet).id, s.veil.intention());
  return true;
}

}  // namespace orcsdr::dead_air_app

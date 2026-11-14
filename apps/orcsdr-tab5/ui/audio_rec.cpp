#include "audio_rec.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "buffer_claim.hpp"
#include "memory_reclaim.hpp"

namespace orcsdr::audio_rec {
namespace {

int16_t* g_buf = nullptr;
size_t g_capacity = 0;
std::atomic<size_t> g_write{0};
std::atomic<bool> g_active{false};
std::atomic<bool> g_full{false};
std::atomic<bool> g_export_pending{false};  // the buffer filled: service() saves it
std::atomic<uint32_t> g_writers{0};         // DSP callers inside append() right now
std::atomic<bool> g_saving{false};          // a save is queued or being written: the buffer must stay
std::atomic<uint8_t> g_band{0};
std::atomic<uint32_t> g_frequency_hz{0};
uint32_t g_file_seq = 0;
char g_last_path[64] = "";
buffer_claim::Claim g_claim;                // held to arm, stop or free the buffer
const Hooks* g_hooks = nullptr;             // for the reclaim holder, which takes no arguments

// The save, driven from the main loop.
bool g_waiting_for_writer = false;
char g_path[64] = "";
uint8_t g_header[kWavHeaderBytes] = {};
size_t g_export_samples = 0;

void say(const Hooks& hooks, const char* format, ...) {
  char line[256];
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  hooks.write(line);
}

void put_le16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xff);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
}
void put_le32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xff);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xff);
}

buffer_claim::CaptureState state() {
  buffer_claim::CaptureState s;
  s.allocated = g_buf != nullptr;
  s.active = g_active.load();
  s.writing = g_writers.load() != 0;
  s.pending = g_export_pending.load() || g_saving.load();
  s.samples = g_write.load();
  s.saved = g_last_path[0] != '\0';
  return s;
}

bool ensure_buffer(const Hooks& hooks) {
  if (g_buf != nullptr && g_capacity >= kMaxSamples) return true;
  if (g_buf != nullptr) {
    hooks.release(g_buf);
    g_buf = nullptr;
    g_capacity = 0;
  }
  g_buf = hooks.alloc_psram(kMaxSamples * sizeof(int16_t));
  if (g_buf == nullptr) g_buf = hooks.alloc_internal(kMaxSamples * sizeof(int16_t));
  if (g_buf == nullptr) {
    hooks.write_line("RTL_REC_ERR no_buffer");
    return false;
  }
  g_capacity = kMaxSamples;
  say(hooks, "RTL_REC_BUF samples=%u bytes=%u psram=%s\n", static_cast<unsigned>(g_capacity),
      static_cast<unsigned>(g_capacity * sizeof(int16_t)), hooks.psram_has_room() ? "yes" : "no");
  return true;
}

void try_submit(const Hooks& hooks) {
  if (g_waiting_for_writer &&
      hooks.writer().submit(g_path, g_header, kWavHeaderBytes, g_buf, g_export_samples * sizeof(int16_t)))
    g_waiting_for_writer = false;
}

bool hold_in_psram(const Hooks& hooks, size_t samples) {
  /* No SD: keep PCM in PSRAM; operator can re-insert card and RTL_REC_SAVE. */
  say(hooks, "RTL_REC_PSRAM_HOLD samples=%u rate=%u note=insert_sd_then_RTL_REC_SAVE\n",
      static_cast<unsigned>(samples), static_cast<unsigned>(kRateHz));
  return false;
}

void append_guarded(const int16_t* samples, size_t count, const Hooks& hooks) {
  if (g_buf == nullptr) return;
  size_t w = g_write.load(std::memory_order_relaxed);
  const size_t cap = g_capacity;
  if (w >= cap) {
    g_full.store(true, std::memory_order_release);
    g_active.store(false, std::memory_order_release);
    return;
  }
  const size_t n = (w + count > cap) ? (cap - w) : count;
  std::memcpy(g_buf + w, samples, n * sizeof(int16_t));
  w += n;
  g_write.store(w, std::memory_order_release);
  if (w >= cap) {
    g_full.store(true, std::memory_order_release);
    g_active.store(false, std::memory_order_release);
    g_export_pending.store(true, std::memory_order_release);
    say(hooks, "RTL_REC_FULL samples=%u sec=%.1f\n", static_cast<unsigned>(w),
        static_cast<double>(w) / static_cast<double>(kRateHz));
  }
}

bool stop_impl(const Hooks& hooks) {
  if (g_saving.load()) {  // the previous recording is still being written to the card
    hooks.write_line("RTL_REC_STOP busy=saving");
    return true;
  }
  g_claim.take(hooks.wait);  // the buffer cannot be released while it is being stopped and handed to the writer
  buffer_claim::Hold hold(g_claim);
  g_export_pending.store(false);  // this stop saves it; the automatic save must not run again
  const bool was = g_active.exchange(false, std::memory_order_acq_rel);
  while (g_writers.load(std::memory_order_acquire) != 0) hooks.wait();
  const size_t samples = g_write.load(std::memory_order_acquire);
  const bool full = g_full.load(std::memory_order_acquire);
  if (!was && g_last_path[0]) {
    say(hooks, "RTL_REC_STOP already_saved path=%s\n", g_last_path);
    return true;
  }
  if (!was && samples == 0) {
    hooks.write_line("RTL_REC_STOP empty");
    return false;
  }
  const uint8_t band = g_band.load();
  say(hooks, "RTL_REC_STOP samples=%u sec=%.2f full=%s band=%s frequency_hz=%u\n", static_cast<unsigned>(samples),
      static_cast<double>(samples) / static_cast<double>(kRateHz), full ? "true" : "false", hooks.band_name(band),
      static_cast<unsigned>(g_frequency_hz.load()));
  if (samples == 0 || g_buf == nullptr) return false;

  const bool sd_ready = hooks.sd_ready();
  bool free_slot = false;
  for (size_t attempt = 0; attempt < 999; ++attempt) {
    g_file_seq = g_file_seq % 999 + 1;
    std::snprintf(g_path, sizeof(g_path), "/orcsdr/rec_%03u_%s_%u.wav", static_cast<unsigned>(g_file_seq),
                  hooks.band_name(band), static_cast<unsigned>(g_frequency_hz.load()));
    if (!sd_ready || !hooks.exists(g_path)) {
      free_slot = true;
      break;
    }
  }
  if (!free_slot) {
    hooks.write_line("RTL_REC_WAV_ERR no_free_slot");
    return false;
  }
  if (!hooks.sd_ready()) return hold_in_psram(hooks, samples);
  (void)hooks.mkdir("/orcsdr");
  // The card's write mode appends; sequence numbers restart after boot, so replace collisions.
  if (hooks.exists(g_path) && !hooks.remove(g_path)) {
    say(hooks, "RTL_REC_WAV_ERR replace path=%s\n", g_path);
    return hold_in_psram(hooks, samples);
  }
  wav_header(g_header, samples);
  g_export_samples = samples;
  g_saving.store(true);  // before the claim is given back: a reclaim must now leave the buffer alone
  g_waiting_for_writer = true;
  try_submit(hooks);
  return true;  // RTL_REC_WAV ok follows from service()
}

void finish(const Hooks& hooks) {
  g_saving.store(false);
  hooks.ui_refresh();  // the radio controls and the Capture panel show the new state and the file
}

}  // namespace

void wav_header(uint8_t out[kWavHeaderBytes], size_t samples) {
  const uint32_t data_bytes = static_cast<uint32_t>(samples * sizeof(int16_t));
  std::memcpy(out, "RIFF", 4);
  put_le32(out + 4, 36u + data_bytes);
  std::memcpy(out + 8, "WAVE", 4);
  std::memcpy(out + 12, "fmt ", 4);
  put_le32(out + 16, 16u);
  put_le16(out + 20, 1u);           /* PCM */
  put_le16(out + 22, 1u);           /* mono */
  put_le32(out + 24, kRateHz);
  put_le32(out + 28, kRateHz * 2u); /* byte rate, mono int16 */
  put_le16(out + 32, 2u);           /* block align */
  put_le16(out + 34, 16u);          /* bits */
  std::memcpy(out + 36, "data", 4);
  put_le32(out + 40, data_bytes);
}

bool start(const Hooks& hooks) {
  if (hooks.band_is_lora()) {
    hooks.write_line("RTL_REC_ERROR data_mode_use_RTL_IQ_START");
    return false;
  }
  if (g_active.load(std::memory_order_acquire)) {
    hooks.write_line("RTL_REC_ALREADY");
    return true;
  }
  if (g_saving.load() || g_export_pending.load()) {  // the previous recording is not on the card yet
    hooks.write_line("RTL_REC_ERROR saving");
    return false;
  }
  g_hooks = &hooks;
  (void)memory_reclaim::register_holder({"Audio recording", held_bytes, release_buffer});
  g_claim.take(hooks.wait);
  buffer_claim::Hold hold(g_claim);
  if (!ensure_buffer(hooks)) return false;
  g_write.store(0, std::memory_order_release);
  g_full.store(false, std::memory_order_release);
  g_band.store(hooks.band());
  g_frequency_hz.store(hooks.frequency_hz());
  g_last_path[0] = '\0';
  g_active.store(true, std::memory_order_release);
  say(hooks, "RTL_REC_START band=%s frequency_hz=%u rate=%u max_sec=%u\n", hooks.band_name(g_band.load()),
      static_cast<unsigned>(g_frequency_hz.load()), static_cast<unsigned>(kRateHz),
      static_cast<unsigned>(kMaxSeconds));
  return true;
}

bool stop_and_export(const Hooks& hooks) { return stop_impl(hooks); }

void service(const Hooks& hooks) {
  if (g_export_pending.load() && !g_saving.load() && g_export_pending.exchange(false)) {
    /* Save automatically after the buffer fills (never write SD from the audio or radio tasks). */
    if (!stop_impl(hooks)) hooks.ui_refresh();
  }
  if (!g_saving.load()) return;
  if (g_waiting_for_writer) {
    try_submit(hooks);
    return;
  }
  sd_writer::Writer& writer = hooks.writer();
  const sd_writer::Result result = writer.result();
  if (result.state != sd_writer::State::done && result.state != sd_writer::State::failed) return;
  writer.acknowledge();
  const size_t data_bytes = g_export_samples * sizeof(int16_t);
  if (result.failure == sd_writer::Failure::open) {
    say(hooks, "RTL_REC_WAV_ERR open path=%s\n", g_path);
    (void)hold_in_psram(hooks, g_export_samples);
  } else if (result.written != kWavHeaderBytes + data_bytes) {
    const size_t got = result.written > kWavHeaderBytes ? result.written - kWavHeaderBytes : 0;
    say(hooks, "RTL_REC_WAV_ERR short_write got=%u want=%u\n", static_cast<unsigned>(got),
        static_cast<unsigned>(data_bytes));
    (void)hold_in_psram(hooks, g_export_samples);
  } else {
    std::snprintf(g_last_path, sizeof(g_last_path), "%s", g_path);
    say(hooks, "RTL_REC_WAV ok path=%s samples=%u rate=%u channels=1 bits=16 note=post_demod_pcm\n", g_path,
        static_cast<unsigned>(g_export_samples), static_cast<unsigned>(kRateHz));
  }
  finish(hooks);
}

void status_print(const Hooks& hooks) {
  const size_t samples = g_write.load(std::memory_order_acquire);
  say(hooks,
      "RTL_REC_STATUS active=%s full=%s samples=%u sec=%.2f max_sec=%u rate=%u "
      "band=%s frequency_hz=%u last_path=\"%s\" sd=%s\n",
      g_active.load(std::memory_order_acquire) ? "true" : "false",
      g_full.load(std::memory_order_acquire) ? "true" : "false", static_cast<unsigned>(samples),
      static_cast<double>(samples) / static_cast<double>(kRateHz), static_cast<unsigned>(kMaxSeconds),
      static_cast<unsigned>(kRateHz), hooks.band_name(g_band.load()), static_cast<unsigned>(g_frequency_hz.load()),
      g_last_path[0] ? g_last_path : "none", hooks.sd_state());
}

void append(const int16_t* samples, size_t count, const Hooks& hooks) {
  if (samples == nullptr || count == 0) return;
  if (!g_active.load(std::memory_order_relaxed)) return;
  // Announce the writer, then look at `active` again: a stop or release on another task sees one or the other.
  g_writers.fetch_add(1, std::memory_order_seq_cst);
  if (g_active.load(std::memory_order_seq_cst)) append_guarded(samples, count, hooks);
  g_writers.fetch_sub(1, std::memory_order_seq_cst);
}

bool active() { return g_active.load(std::memory_order_acquire); }
bool full() { return g_full.load(std::memory_order_acquire); }
size_t samples() { return g_write.load(std::memory_order_acquire); }
uint8_t band() { return g_band.load(); }
uint32_t frequency_hz() { return g_frequency_hz.load(); }
const char* last_path() { return g_last_path; }
bool saving() { return g_saving.load(); }
const void* buffer() { return g_buf; }
size_t buffer_bytes() { return g_capacity * sizeof(int16_t); }

size_t held_bytes() { return buffer_claim::releasable(state()) ? g_capacity * sizeof(int16_t) : 0u; }

size_t release_buffer() {
  if (!g_claim.try_take()) return 0;  // being armed or stopped right now
  buffer_claim::Hold hold(g_claim);
  if (!buffer_claim::releasable(state()) || g_hooks == nullptr) return 0;
  const size_t bytes = g_capacity * sizeof(int16_t);
  int16_t* buffer = g_buf;
  g_buf = nullptr;  // clear first: every user checks for null and the next recording allocates it again
  g_capacity = 0;
  g_hooks->release(buffer);
  return bytes;
}

void reset_for_tests() {
  g_buf = nullptr;
  g_capacity = 0;
  g_write.store(0);
  g_active.store(false);
  g_full.store(false);
  g_export_pending.store(false);
  g_writers.store(0);
  g_saving.store(false);
  g_band.store(0);
  g_frequency_hz.store(0);
  g_file_seq = 0;
  g_last_path[0] = '\0';
  g_hooks = nullptr;
  g_waiting_for_writer = false;
  g_export_samples = 0;
  if (g_claim.held()) g_claim.give();
}

}  // namespace orcsdr::audio_rec

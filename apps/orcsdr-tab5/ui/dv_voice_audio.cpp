#include "dv_voice_audio.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

extern "C" {
#include "orcsdr_vocoder.h"
}

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <esp_timer.h>
#else
#include <chrono>
#endif

namespace orcsdr::dv_voice {
namespace {
constexpr size_t kFrameSamples8k = 160;
constexpr size_t kUp = 6;                       // 8 kHz -> 48 kHz
constexpr size_t kTapsPerPhase = 32;
constexpr size_t kTaps = kUp * kTapsPerPhase;   // 192
constexpr size_t kFramesPerGroup = 18;
constexpr size_t kLatencyQueue = 8;

float* g_taps = nullptr;   // 768 B, allocated once in PSRAM: the Tab5 internal RAM boot pool has no room for statics
void* alloc_psram(size_t bytes);
void build_taps() {
  if (g_taps) return;
  g_taps = static_cast<float*>(alloc_psram(sizeof(float) * kTaps));
  if (!g_taps) return;
  // Hann-windowed sinc, cutoff 3.9 kHz of the 48 kHz output rate (passes telephone speech, rejects the 4-12 kHz images).
  const double pi = 3.14159265358979323846, fc = 3900.0 / 48000.0;
  double sum = 0;
  double tmp[kTaps];
  for (size_t n = 0; n < kTaps; ++n) {
    const double x = static_cast<double>(n) - (static_cast<double>(kTaps) - 1.0) / 2.0;
    const double s = std::fabs(x) < 1e-9 ? 2.0 * fc : std::sin(2.0 * pi * fc * x) / (pi * x);
    const double w = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(n) / (static_cast<double>(kTaps) - 1.0));
    tmp[n] = s * w;
    sum += tmp[n];
  }
  for (size_t n = 0; n < kTaps; ++n) g_taps[n] = static_cast<float>(tmp[n] * static_cast<double>(kUp) / sum);
}

void* alloc_psram(size_t bytes) {
#if defined(ESP_PLATFORM)
  return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::calloc(1, bytes);
#endif
}
void free_psram(void* p) {
#if defined(ESP_PLATFORM)
  heap_caps_free(p);
#else
  std::free(p);
#endif
}
uint32_t micros() {
#if defined(ESP_PLATFORM)
  return static_cast<uint32_t>(esp_timer_get_time());
#else
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
}  // namespace

const char* listen_name(Listen l) {
  switch (l) {
    case Listen::automatic: return "AUTO";
    case Listen::ts1: return "TS1";
    case Listen::ts2: return "TS2";
    case Listen::mute: return "MUTE";
  }
  return "?";
}
const char* phase_name(Phase p) {
  switch (p) {
    case Phase::listening: return "LISTENING";
    case Phase::acquiring: return "ACQUIRING VOICE";
    case Phase::buffering: return "BUFFERING";
    case Phase::decoding: return "DECODING SPEECH";
    case Phase::silence_frame: return "SILENCE FRAME";
    case Phase::degraded: return "DEGRADED AUDIO";
    case Phase::call_ended: return "CALL ENDED";
    case Phase::protected_voice: return "PROTECTED VOICE";
    case Phase::privacy_unknown: return "PRIVACY UNKNOWN";
    case Phase::muted: return "AUDIO MUTED";
    case Phase::unavailable: return "AUDIO UNAVAILABLE";
  }
  return "?";
}

struct Engine::Slot {
  orc_vocoder codec;
  float hist[kTapsPerPhase - 1];
  uint32_t serial;
  bool live;
  struct Lat { uint64_t pos; uint32_t ms; } lat[kLatencyQueue];
  uint8_t lat_r, lat_w;
};

bool Engine::begin(const Config& cfg) {
  end();
  build_taps();
  if (!g_taps) return false;
  cfg_ = cfg;
  ring_cap_ = static_cast<size_t>(cfg.ring_ms) * kOutRate / 1000u;
  slots_ = static_cast<Slot*>(alloc_psram(sizeof(Slot) * 3));
  ring_ = static_cast<int16_t*>(alloc_psram(ring_cap_ * sizeof(int16_t)));
  if (!slots_ || !ring_) {
    end();
    return false;
  }
  for (int i = 0; i < 3; ++i) orc_vocoder_reset(&slots_[i].codec);
  stats_ = {};
  synth_total_us_ = 0;
  rd_ = wr_ = 0;
  playing_ = false;
  voiced_slot_ = 0;
  return true;
}
void Engine::end() {
  if (ring_) free_psram(ring_);
  if (slots_) free_psram(slots_);
  ring_ = nullptr;
  slots_ = nullptr;
  ring_cap_ = 0;
}

void Engine::set_enabled(bool on) {
  enabled_ = on;
  if (!on) reset_stream();
}
void Engine::set_listen(Listen l) {
  if (l == listen_) return;
  listen_ = l;
  // A new selection never inherits audio from the previous one.
  drop_playout();
  voiced_slot_ = 0;
}
void Engine::set_muted(bool m) {
  muted_ = m;
  if (m) drop_playout();
}

bool Engine::selected(uint8_t slot) const {
  if (!enabled_ || muted_ || listen_ == Listen::mute) return false;
  switch (listen_) {
    case Listen::ts1: return slot == 1;
    case Listen::ts2: return slot == 2;
    default: return voiced_slot_ == 0 || voiced_slot_ == slot;
  }
}

void Engine::drop_playout() {
  pending_active_ = false;       // a group being synthesized belongs to the audio just dropped
  rd_ = wr_;
  playing_ = false;
  latency_pending_ = false;
  if (slots_) for (int i = 0; i < 3; ++i) slots_[i].lat_r = slots_[i].lat_w = 0;
}
void Engine::reset_stream() {
  drop_playout();
  voiced_slot_ = 0;
  voiced_serial_ = 0;
  for (auto& p : privacy_) p = 0;
  if (slots_) for (int i = 0; i < 3; ++i) { orc_vocoder_reset(&slots_[i].codec); slots_[i].live = false; std::memset(slots_[i].hist, 0, sizeof(slots_[i].hist)); }
}
size_t Engine::buffered_samples() const { return static_cast<size_t>(wr_ - rd_); }

void Engine::push_frame(uint8_t slot, const int16_t pcm[160], bool silent) {
  Slot& s = slots_[slot];
  if (speech_sink_ && slot >= 1 && slot <= 2) speech_sink_(slot, s.serial, src_, dst_, pcm, silent, pending_now_ms_, speech_user_);
  if (ring_cap_ - static_cast<size_t>(wr_ - rd_) < kFrameSamples8k * kUp) { ++stats_.overruns; return; }
  float buf[kTapsPerPhase - 1 + kFrameSamples8k];
  std::memcpy(buf, s.hist, sizeof(s.hist));
  for (size_t i = 0; i < kFrameSamples8k; ++i) buf[kTapsPerPhase - 1 + i] = silent ? 0.0f : static_cast<float>(pcm[i]) * gain_;
  for (size_t i = 0; i < kFrameSamples8k; ++i) {
    const float* x = buf + kTapsPerPhase - 1 + i;
    for (size_t p = 0; p < kUp; ++p) {
      float acc = 0.0f;
      for (size_t k = 0; k < kTapsPerPhase; ++k) acc += g_taps[p + kUp * k] * x[-static_cast<ptrdiff_t>(k)];
      int32_t v = static_cast<int32_t>(acc >= 0.0f ? acc + 0.5f : acc - 0.5f);
      if (v > 32767) { v = 32767; ++stats_.clipped_samples; }
      if (v < -32768) { v = -32768; ++stats_.clipped_samples; }
      ring_[static_cast<size_t>(wr_ % ring_cap_)] = static_cast<int16_t>(v);
      ++wr_;
    }
  }
  std::memcpy(s.hist, buf + kFrameSamples8k, sizeof(s.hist));
  stats_.samples_in += kFrameSamples8k * kUp;
}

void Engine::emit_silence(uint8_t slot, size_t frames8k) {
  static const int16_t zeros[kFrameSamples8k] = {0};
  for (size_t i = 0; i < frames8k; ++i) push_frame(slot, zeros, true);
  stats_.frames_concealed += frames8k;
}

bool Engine::inject_test_pcm(const int16_t* pcm8k, size_t n, uint32_t now_ms) {
  if (!running() || !pcm8k || n == 0 || n % kFrameSamples8k) return false;
  if (wr_ == rd_) { first_group_ms_ = now_ms; playing_ = false; }
  for (size_t i = 0; i < n; i += kFrameSamples8k) push_frame(0, pcm8k + i, false);
  stats_.test_samples += n;
  return true;
}

void Engine::begin_group(const dv_radio::VoiceMsg& m, uint32_t now_ms) {
  Slot& s = slots_[m.slot];
  const uint32_t t0 = micros();
  pending_now_ms_ = now_ms;
  pending_first_sample_ = wr_;
  if (m.new_call || !s.live || s.serial != m.call_serial) {
    orc_vocoder_reset(&s.codec);
    std::memset(s.hist, 0, sizeof(s.hist));
    s.live = true;
    s.serial = m.call_serial;
    ++stats_.calls;
  } else if (m.lost_superframes) {
    // Whole superframes are missing: the predictor state is stale. Fill with silence, never with repeated speech.
    orc_vocoder_reset(&s.codec);
    std::memset(s.hist, 0, sizeof(s.hist));
    emit_silence(m.slot, static_cast<size_t>(m.lost_superframes) * kFramesPerGroup);
    stats_.lost_superframes += m.lost_superframes;
  }
  pending_ = m;
  pending_active_ = true;
  pending_next_ = 0;
  pending_sq_ = 0;
  pending_peak_ = 0;
  pending_samples_ = 0;
  pending_bad_ = false;
  pending_silent_only_ = true;
  pending_us_ = micros() - t0;
  pending_now_ms_ = now_ms;
}

size_t Engine::service(uint32_t now_ms, size_t max_frames) {
  if (!pending_active_ || !running()) return 0;
  const uint32_t t0 = micros();
  const dv_radio::VoiceMsg& m = pending_;
  Slot& s = slots_[m.slot];
  size_t done = 0;
  while (done < max_frames && pending_next_ < kFramesPerGroup) {
    const size_t b = pending_next_ / 3, f = pending_next_ % 3;
    const uint8_t* wire = m.payload + b * 27 + f * 9;
    int16_t pcm[kFrameSamples8k];
    orc_vocoder_result r;
    const int ok = orc_vocoder_dmr(&s.codec, wire, ORC_PRIVACY_CLEAR, pcm, &r);
    ++stats_.frames;
    const bool error = !ok || std::strchr(r.flags, 'E') != nullptr;
    const bool silence = !error && std::strchr(r.flags, 'S') != nullptr;
    if (tap_) tap_(m.slot, m.call_serial, wire, pcm, r.corrected, r.total, r.flags, tap_user_);
    stats_.corrected_bits += static_cast<uint64_t>(r.corrected);
    stats_.total_corrected_bits += static_cast<uint64_t>(r.total);
    if (error) {
      ++stats_.frames_error;
      ++stats_.frames_concealed;
      pending_bad_ = true;
      push_frame(m.slot, pcm, true);          // uncorrectable: silence, codec already reset by the adapter
    } else if (silence) {
      ++stats_.frames_silence;
      push_frame(m.slot, pcm, true);
    } else {
      ++stats_.frames_speech;
      pending_silent_only_ = false;
      if (r.total >= 8) pending_bad_ = true;
      push_frame(m.slot, pcm, false);
      for (size_t i = 0; i < kFrameSamples8k; ++i) {
        const int32_t a = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (a > pending_peak_) pending_peak_ = a;
        pending_sq_ += static_cast<int64_t>(pcm[i]) * pcm[i];
      }
      pending_samples_ += kFrameSamples8k;
    }
    ++pending_next_;
    ++done;
  }
  pending_us_ += micros() - t0;
  if (pending_next_ >= kFramesPerGroup) {
    (void)now_ms;
    finish_group();
  }
  return done;
}

void Engine::finish_group() {
  const dv_radio::VoiceMsg& m = pending_;
  ++stats_.groups;
  stats_.peak_last = pending_peak_;
  stats_.rms_last = pending_samples_ ? static_cast<int32_t>(std::sqrt(static_cast<double>(pending_sq_) / static_cast<double>(pending_samples_))) : 0;
  if (pending_bad_) last_degraded_ms_ = pending_now_ms_;
  if (pending_silent_only_) last_silence_ms_ = pending_now_ms_;
  stats_.synth_us_last = pending_us_;   // CPU time spent on this group (the slices added up), not wall time
  if (pending_us_ > stats_.synth_us_max) stats_.synth_us_max = pending_us_;
  synth_total_us_ += pending_us_;
  stats_.synth_us_avg = static_cast<uint32_t>(synth_total_us_ / stats_.groups);
  // Latency bookkeeping: when the read position reaches this group's first sample we log now - arrival.
  Slot& v = slots_[m.slot];
  const uint8_t next = static_cast<uint8_t>((v.lat_w + 1) % kLatencyQueue);
  if (next != v.lat_r) { v.lat[v.lat_w] = {pending_first_sample_, pending_now_ms_}; v.lat_w = next; }
  pending_active_ = false;
}

void Engine::handle(const dv_radio::VoiceMsg& m, uint32_t now_ms, bool defer) {
  if (!running()) return;
  if (pending_active_ && m.kind != dv_radio::VoiceKind::flush) (void)service(now_ms, kFramesPerGroup);   // finish the previous group first, in order
  using K = dv_radio::VoiceKind;
  switch (m.kind) {
    case K::flush:
      ++stats_.flushes;
      reset_stream();
      return;
    case K::call_ended:
      if (m.slot >= 1 && m.slot <= 2) {
        if (m.slot == voiced_slot_) { voiced_slot_ = 0; last_end_ms_ = now_ms; }
        if (privacy_[m.slot] == 1) privacy_[m.slot] = 0;
        slots_[m.slot].live = false;
      }
      return;
    case K::protected_voice:
      if (m.slot >= 1 && m.slot <= 2) privacy_[m.slot] = 2;
      last_protected_ms_ = now_ms;
      if (m.slot == voiced_slot_) { voiced_slot_ = 0; slots_[m.slot].live = false; }
      return;
    case K::unqualified:
      if (m.slot >= 1 && m.slot <= 2) privacy_[m.slot] = 0;
      last_unqualified_ms_ = now_ms;
      if (m.slot == voiced_slot_) { voiced_slot_ = 0; slots_[m.slot].live = false; }
      return;
    case K::group:
      break;
  }
  if (m.slot < 1 || m.slot > 2) return;
  privacy_[m.slot] = 1;
  if (!selected(m.slot)) { ++stats_.groups_dropped_not_selected; return; }
  if (voiced_slot_ != m.slot) {
    voiced_slot_ = m.slot;
    voiced_serial_ = m.call_serial;
  }
  src_ = m.source_id; dst_ = m.destination_id; group_call_ = m.group_call; cc_ = m.colour_code;
  if (wr_ == rd_) { first_group_ms_ = now_ms; playing_ = false; }
  if (last_group_ms_ != 0 && !m.new_call && m.lost_superframes == 0 && now_ms - last_group_ms_ < 1500) {
    const uint32_t gap = now_ms - last_group_ms_;
    if (stats_.gap_min_ms == 0 || gap < stats_.gap_min_ms) stats_.gap_min_ms = gap;
    if (gap > stats_.gap_max_ms) stats_.gap_max_ms = gap;
  }
  last_group_ms_ = now_ms;
  begin_group(m, now_ms);
  if (!defer) (void)service(now_ms, kFramesPerGroup);
}

size_t Engine::pull(int16_t* out, size_t n, uint32_t now_ms) {
  if (!running() || !out || n == 0) return 0;
  if (voiced_slot_ && now_ms - last_group_ms_ > 2500u) { voiced_slot_ = 0; last_end_ms_ = now_ms; }   // no end notice arrived
  if (!playing_) {
    if (wr_ == rd_) return 0;
    if (now_ms - first_group_ms_ < cfg_.target_buffer_ms) return 0;
    playing_ = true;
  }
  size_t avail = static_cast<size_t>(wr_ - rd_);
  if (avail == 0) {
    if (voiced_slot_ != 0) ++stats_.underruns;     // dry in the middle of a call: rebuffer before resuming
    playing_ = false;
    return 0;
  }
  const size_t take = n < avail ? n : avail;
  for (size_t i = 0; i < take; ++i) out[i] = ring_[static_cast<size_t>((rd_ + i) % ring_cap_)];
  rd_ += take;
  stats_.samples_out += take;
  for (int sl = 1; sl <= 2; ++sl) {
    Slot& v = slots_[sl];
    while (v.lat_r != v.lat_w && v.lat[v.lat_r].pos < rd_) {
      stats_.latency_ms_last = now_ms - v.lat[v.lat_r].ms;
      v.lat_r = static_cast<uint8_t>((v.lat_r + 1) % kLatencyQueue);
    }
  }
  const uint32_t ms = static_cast<uint32_t>((wr_ - rd_) * 1000u / kOutRate);
  if (ms > stats_.buffered_peak_ms) stats_.buffered_peak_ms = ms;
  return take;
}

Snapshot Engine::snapshot(uint32_t now_ms) const {
  Snapshot s;
  s.listen = listen_;
  s.enabled = enabled_;
  s.muted = muted_;
  s.playing = playing_;
  s.active_slot = voiced_slot_;
  for (int i = 0; i < 3; ++i) s.privacy[i] = privacy_[i];
  s.source_id = src_; s.destination_id = dst_; s.group_call = group_call_; s.colour_code = cc_;
  s.stats = stats_;
  s.stats.buffered_ms = static_cast<uint32_t>((wr_ - rd_) * 1000u / kOutRate);
  s.stats.target_buffer_ms = cfg_.target_buffer_ms;
  auto recent = [&](uint32_t t, uint32_t hold) { return t != 0 && now_ms - t < hold; };
  if (!running()) s.phase = Phase::unavailable;
  else if (!enabled_ || muted_ || listen_ == Listen::mute) s.phase = Phase::muted;
  else if (recent(last_protected_ms_, 2500)) s.phase = Phase::protected_voice;
  else if (voiced_slot_ != 0) {
    if (!playing_) s.phase = Phase::buffering;
    else if (recent(last_degraded_ms_, cfg_.degraded_hold_ms)) s.phase = Phase::degraded;
    else if (recent(last_silence_ms_, 1000)) s.phase = Phase::silence_frame;
    else s.phase = Phase::decoding;
  } else if (wr_ != rd_) s.phase = Phase::decoding;   // tail of a finished call still draining
  else if (recent(last_end_ms_, cfg_.call_end_hold_ms)) s.phase = Phase::call_ended;
  else if (recent(last_unqualified_ms_, 2500)) s.phase = Phase::privacy_unknown;
  else s.phase = Phase::listening;
  return s;
}

}  // namespace orcsdr::dv_voice

#include "audio_service.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hosyond_board.h"
#include "radio_service.hpp"

// The demodulator runs inside the receiver task's IQ callback, straight on the block it just read: no extra copies of
// the 2.4 MS/s stream. It produces 48 kS/s audio into a small lock-free ring; the audio task resamples (to absorb the
// few hundred ppm between the dongle's clock and the codec's) and feeds the ES8311.
namespace audio_service {
namespace {
constexpr char TAG[] = "AUDIO";
constexpr uint32_t kRate = 48000;
constexpr size_t kRingSize = 16384;  // audio samples (power of two), about 340 ms
constexpr int kFramesPerWrite = 240;  // 5 ms of audio per I2S write
constexpr int kTargetQueued = 1200;   // about 25 ms of audio queued is the steady state
// Output gain: Home's volume 0-255 maps to 0..1 through a squared curve (about -12 dB at the middle step), with the
// codec at full output: the whole range of the amplifier and speaker is available.
constexpr float kMaxGain = 1.0f;

orcsdr::dsp::Demod* g_demod = nullptr;  // used only by the receiver task (the IQ callback)
float* g_ring = nullptr;
std::atomic<uint32_t> g_head{0}, g_tail{0};  // producer (receiver task) / consumer (audio task)
float* g_tmp = nullptr;                      // demodulator output scratch, 2048 samples

std::atomic<int> g_mode{static_cast<int>(orcsdr::dsp::Mode::wfm)};
std::atomic<bool> g_mode_supported{true};
std::atomic<uint8_t> g_volume{128};
std::atomic<bool> g_sound{true};
std::atomic<bool> g_gate{true};
std::atomic<bool> g_active{true};
std::atomic<float> g_channel_dbfs{-120.0f};
std::atomic<uint32_t> g_blocks{0}, g_dropped{0}, g_underruns{0};
std::atomic<uint32_t> g_tone_hz{0}, g_tone_frames{0};
std::atomic<float> g_fill{0.0f}, g_ppm{0.0f};
std::atomic<uint32_t> g_demod_us{0}, g_write_us{0}, g_resample_us{0};  // smoothed times
bool g_running = false;

// Runs on the receiver task for every block of IQ.
void iq_sink(const uint8_t* iq, size_t bytes) {
  if (!g_demod || !g_active.load()) return;
  static orcsdr::dsp::Mode current_mode = orcsdr::dsp::Mode::wfm;
  const auto mode = static_cast<orcsdr::dsp::Mode>(g_mode.load());
  if (mode != current_mode) {
    g_demod->set_mode(mode);
    current_mode = mode;
  }
  const uint32_t rate = orcsdr::radio::sample_rate();
  if (rate != g_demod->input_rate() && rate >= 240000) g_demod->set_input_rate(rate);
  g_blocks.fetch_add(1);

  const int64_t t0 = esp_timer_get_time();
  size_t offset = 0;
  while (offset + 1 < bytes) {
    const size_t part = std::min<size_t>(bytes - offset, 8192);  // keeps the output within g_tmp
    const size_t n = g_demod->process(iq + offset, part & ~static_cast<size_t>(1), g_tmp, 2048);
    offset += part;
    uint32_t head = g_head.load(std::memory_order_relaxed);
    const uint32_t tail = g_tail.load(std::memory_order_acquire);
    if (kRingSize - (head - tail) < n) {
      g_dropped.fetch_add(1);
      continue;
    }
    for (size_t i = 0; i < n; ++i) g_ring[(head + i) & (kRingSize - 1)] = g_tmp[i];
    g_head.store(head + static_cast<uint32_t>(n), std::memory_order_release);
  }
  g_demod_us.store((g_demod_us.load() * 15 + static_cast<uint32_t>(esp_timer_get_time() - t0)) / 16);
  g_channel_dbfs.store(g_demod->channel_dbfs());
}

float gain() {
  if (!g_sound.load()) return 0.0f;
  const float v = g_volume.load() / 255.0f;
  return kMaxGain * v * v;
}

void write_mono(const float* mono, int frames, float g) {
  int16_t out[kFramesPerWrite * 2];
  for (int i = 0; i < frames; ++i) {
    const float v = std::clamp(mono[i] * g, -1.0f, 1.0f);
    const int16_t s = static_cast<int16_t>(v * 32767.0f);
    out[2 * i] = s;
    out[2 * i + 1] = s;
  }
  hosyond_audio_write(out, frames);
}

void task(void*) {
  // Resampler state: the demodulated audio is consumed at `step` samples per output sample; step stays within
  // +/-0.2% of 1. Single precision throughout: the FPU is float-only and doubles are software-emulated.
  std::vector<float> pending;
  pending.reserve(4096);
  float position = 0.0f;
  float last = 0.0f;
  float chunk[kFramesPerWrite];
  float tone_phase = 0.0f;
  int64_t starved_since_us = 0;

  for (;;) {
    // A requested test tone takes the speaker until it has played.
    if (g_tone_frames.load() > 0) {
      const uint32_t hz = g_tone_hz.load();
      float tone[kFramesPerWrite];
      const uint32_t remaining = g_tone_frames.load();
      const int frames = static_cast<int>(std::min<uint32_t>(remaining, kFramesPerWrite));
      for (int i = 0; i < frames; ++i) {
        tone[i] = std::sin(tone_phase) * 0.5f;
        tone_phase += 2.0f * 3.14159265f * hz / kRate;
        if (tone_phase > 6.2831853f) tone_phase -= 6.2831853f;
      }
      write_mono(tone, frames, kMaxGain * 0.5f);
      g_tone_frames.store(remaining - frames);
      continue;
    }

    // Move everything the demodulator has produced into `pending`.
    const uint32_t head = g_head.load(std::memory_order_acquire);
    uint32_t tail = g_tail.load(std::memory_order_relaxed);
    const uint32_t queued_ring = head - tail;
    if (pending.size() < 8192) {
      for (; tail != head && pending.size() < 8192; ++tail) pending.push_back(g_ring[tail & (kRingSize - 1)]);
      g_tail.store(tail, std::memory_order_release);
    }

    // Need 240 output samples (plus one for interpolation) ahead of `position`.
    const float step_adjust = std::clamp((static_cast<float>(pending.size()) + queued_ring - kTargetQueued) / 4800.0f * 0.002f, -0.002f, 0.002f);
    const float step = 1.0f + step_adjust;
    g_ppm.store(step_adjust * 1e6f);
    g_fill.store(static_cast<float>(pending.size() + queued_ring) / static_cast<float>(kRingSize));
    const float needed = position + step * kFramesPerWrite + 2.0f;
    if (static_cast<float>(pending.size()) < needed) {
      const int64_t now = esp_timer_get_time();
      if (!starved_since_us) starved_since_us = now;
      if (now - starved_since_us > 30000) {
        // No audio for 30 ms (stream stopped, retuning or a digital screen): keep the codec fed with silence.
        float silence[kFramesPerWrite] = {};
        write_mono(silence, kFramesPerWrite, 0.0f);
        g_underruns.fetch_add(1);
        starved_since_us = now;
      } else {
        vTaskDelay(1);
      }
      continue;
    }
    starved_since_us = 0;

    const int64_t t_res = esp_timer_get_time();
    const bool active = g_mode_supported.load() && g_sound.load() && g_gate.load() && g_active.load();
    for (int i = 0; i < kFramesPerWrite; ++i) {
      const int i0 = static_cast<int>(position);
      const float frac = position - static_cast<float>(i0);
      const float a = pending[i0];
      const float b = pending[i0 + 1];
      chunk[i] = active ? a + (b - a) * frac : 0.0f;
      position += step;
    }
    const int consumed = static_cast<int>(position);
    if (consumed > 0) {
      last = pending[consumed - 1];
      pending.erase(pending.begin(), pending.begin() + consumed);
      position -= static_cast<float>(consumed);
    }
    (void)last;
    const int64_t t_w = esp_timer_get_time();
    write_mono(chunk, kFramesPerWrite, gain());
    g_write_us.store((g_write_us.load() * 15 + static_cast<uint32_t>(esp_timer_get_time() - t_w)) / 16);
    g_resample_us.store((g_resample_us.load() * 15 + static_cast<uint32_t>(t_w - t_res)) / 16);
  }
}
}  // namespace

bool start() {
  if (hosyond_audio_start(kRate) != ESP_OK) {
    ESP_LOGE(TAG, "speaker output failed to start; the receiver runs without sound");
    return false;
  }
  g_ring = static_cast<float*>(heap_caps_malloc(kRingSize * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_tmp = static_cast<float*>(heap_caps_malloc(2048 * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  // The demodulator's state (about 19 KB, all inside this one object) lives in PSRAM. Putting it in internal RAM took
  // that RAM away from the graphics library and tanked the Home frame rate (draw time 13 ms -> 182 ms), measured on
  // the board; as one contiguous object it stays cache-friendly in PSRAM and the receiver task has the headroom.
  void* mem = heap_caps_malloc(sizeof(orcsdr::dsp::Demod), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!g_ring || !g_tmp || !mem) {
    ESP_LOGE(TAG, "buffer allocation failed");
    return false;
  }
  g_demod = new (mem) orcsdr::dsp::Demod();
  ESP_LOGI(TAG, "demodulator %u bytes at %p", unsigned(sizeof(orcsdr::dsp::Demod)), mem);
  orcsdr::radio::set_iq_sink(iq_sink);
  g_running = xTaskCreatePinnedToCore(task, "audio", 8192, nullptr, 6, nullptr, 1) == pdPASS;
  ESP_LOGI(TAG, "receiver audio %s (48 kHz mono, demodulated in the receiver task, adaptive resampler)", g_running ? "running" : "FAILED");
  return g_running;
}

void set_mode(orcsdr::dsp::Mode mode, bool supported) {
  g_mode.store(static_cast<int>(mode));
  g_mode_supported.store(supported);
}
void set_volume(uint8_t volume) { g_volume.store(volume); }
void set_sound(bool enabled) { g_sound.store(enabled); }
void set_gate(bool open) { g_gate.store(open); }
void set_active(bool active) { g_active.store(active); }
float channel_dbfs() { return g_channel_dbfs.load(); }

bool play_tone(uint32_t frequency_hz, uint32_t duration_ms) {
  if (!g_running || frequency_hz < 50 || frequency_hz > 8000 || duration_ms == 0 || duration_ms > 5000) return false;
  g_tone_hz.store(frequency_hz);
  g_tone_frames.store(duration_ms * kRate / 1000);
  return true;
}

Status status() {
  Status s;
  s.running = g_running;
  s.muted = !g_sound.load();
  s.volume = g_volume.load();
  s.blocks = g_blocks.load();
  s.dropped_blocks = g_dropped.load();
  s.underruns = g_underruns.load();
  s.buffer_fill = g_fill.load();
  s.resample_ppm = g_ppm.load();
  s.demod_us = g_demod_us.load();
  s.write_us = g_write_us.load();
  s.resample_us = g_resample_us.load();
  return s;
}
}  // namespace audio_service

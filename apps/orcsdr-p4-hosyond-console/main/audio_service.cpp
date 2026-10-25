#include "audio_service.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "hosyond_board.h"
#include "radio_service.hpp"

namespace audio_service {
namespace {
constexpr char TAG[] = "AUDIO";
constexpr uint32_t kRate = 48000;
constexpr size_t kStreamBytes = 256 * 1024;      // about 130 ms of IQ: rides out the FFT and drawing bursts
constexpr size_t kBlockBytes = 16384;
constexpr int kFramesPerWrite = 240;             // 5 ms of audio per I2S write
// Output gain: Home's volume 0-255 maps to 0..kMaxGain. Deliberately conservative: the codec volume
// is also low (see hosyond_audio.c), and the speaker on the connector is small.
constexpr float kMaxGain = 0.7f;

StreamBufferHandle_t g_stream = nullptr;
std::atomic<int> g_mode{static_cast<int>(orcsdr::dsp::Mode::wfm)};
std::atomic<bool> g_mode_supported{true};
std::atomic<uint8_t> g_volume{128};
std::atomic<bool> g_sound{true};
std::atomic<uint32_t> g_blocks{0}, g_dropped{0}, g_underruns{0};
std::atomic<uint32_t> g_tone_hz{0}, g_tone_frames{0};
std::atomic<float> g_fill{0.0f}, g_ppm{0.0f};
bool g_running = false;

void iq_sink(const uint8_t* iq, size_t bytes) {
  if (!g_stream) return;
  // Never block the receiver: if the audio task has fallen behind, drop the block.
  if (xStreamBufferSpacesAvailable(g_stream) < bytes || xStreamBufferSend(g_stream, iq, bytes, 0) != bytes)
    g_dropped.fetch_add(1);
}

float gain() { return g_sound.load() ? kMaxGain * (g_volume.load() / 255.0f) : 0.0f; }

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
  auto* iq = static_cast<uint8_t*>(heap_caps_malloc(kBlockBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* mono = static_cast<float*>(heap_caps_malloc(2048 * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!iq || !mono) {
    ESP_LOGE(TAG, "buffer allocation failed");
    vTaskDelete(nullptr);
    return;
  }
  orcsdr::dsp::Demod demod;
  orcsdr::dsp::Mode current_mode = orcsdr::dsp::Mode::wfm;

  // Resampler state: audio from the demodulator is consumed at `step` input samples per output
  // sample; step stays within +/-0.2% of 1.
  std::vector<float> pending;
  pending.reserve(4096);
  double position = 0.0;
  float last = 0.0f;
  float chunk[kFramesPerWrite];
  int chunk_fill = 0;
  float tone_phase = 0.0f;

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
      write_mono(tone, frames, kMaxGain * 0.6f);
      g_tone_frames.store(remaining - frames);
      continue;
    }

    const auto mode = static_cast<orcsdr::dsp::Mode>(g_mode.load());
    if (mode != current_mode) {
      demod.set_mode(mode);
      current_mode = mode;
      pending.clear();
      position = 0.0;
    }

    const size_t got = xStreamBufferReceive(g_stream, iq, kBlockBytes, pdMS_TO_TICKS(40));
    if (got < 2) {
      g_underruns.fetch_add(1);
      // No IQ (receiver stopped or retuning): keep the codec fed with silence so it stays in step.
      float silence[kFramesPerWrite] = {};
      write_mono(silence, kFramesPerWrite, 0.0f);
      continue;
    }
    g_blocks.fetch_add(1);
    const float fill = static_cast<float>(xStreamBufferBytesAvailable(g_stream)) / static_cast<float>(kStreamBytes);
    g_fill.store(fill);

    float* demodulated = mono;  // heap buffer; a 8 KB array would overflow the task stack
    const size_t n = demod.process(iq, got & ~static_cast<size_t>(1), demodulated, 2048);
    const bool active = g_mode_supported.load() && g_sound.load();
    pending.insert(pending.end(), demodulated, demodulated + n);

    // Adaptive step: more than one block queued -> consume faster (step > 1); too empty -> slower.
    const float error = (fill - 0.1f);  // about 26 KB (80 ms) queued is the steady state
    const float step_adjust = std::clamp(error * 0.02f, -0.002f, 0.002f);
    g_ppm.store(step_adjust * 1e6f);
    const double step = 1.0 + step_adjust;

    // Linear interpolation across `pending` (position is relative to pending[0], with `last` as pending[-1]).
    while (position + 1.0 < static_cast<double>(pending.size())) {
      const int i0 = static_cast<int>(std::floor(position));
      const float frac = static_cast<float>(position - i0);
      const float a = i0 < 0 ? last : pending[i0];
      const float b = pending[i0 + 1];
      chunk[chunk_fill++] = active ? a + (b - a) * frac : 0.0f;
      position += step;
      if (chunk_fill == kFramesPerWrite) {
        write_mono(chunk, chunk_fill, gain());
        chunk_fill = 0;
      }
    }
    const int consumed = std::min(static_cast<int>(std::floor(position)), static_cast<int>(pending.size()) - 1);
    if (consumed > 0) {
      last = pending[consumed - 1];
      pending.erase(pending.begin(), pending.begin() + consumed);
      position -= consumed;
    }
  }
}
}  // namespace

bool start() {
  if (hosyond_audio_start(kRate) != ESP_OK) {
    ESP_LOGE(TAG, "speaker output failed to start; the receiver runs without sound");
    return false;
  }
  g_stream = xStreamBufferCreateWithCaps(kStreamBytes, 1, MALLOC_CAP_SPIRAM);
  if (!g_stream) return false;
  orcsdr::radio::set_iq_sink(iq_sink);
  g_running = xTaskCreatePinnedToCore(task, "audio", 12288, nullptr, 6, nullptr, 1) == pdPASS;
  ESP_LOGI(TAG, "receiver audio %s (48 kHz mono, adaptive resampler)", g_running ? "running" : "FAILED");
  return g_running;
}

void set_mode(orcsdr::dsp::Mode mode, bool supported) {
  g_mode.store(static_cast<int>(mode));
  g_mode_supported.store(supported);
}
void set_volume(uint8_t volume) { g_volume.store(volume); }
void set_sound(bool enabled) { g_sound.store(enabled); }

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
  return s;
}
}  // namespace audio_service

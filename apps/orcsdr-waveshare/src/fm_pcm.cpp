#include "fm_pcm.hpp"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace orcsdr::fm {
namespace {

/* 960k → ×4 → 240k discr → ×5 → 48k (matches Tab5 ratios). */
constexpr uint8_t kRfDecim = 4;
constexpr uint8_t kAudioDecim = 5;
constexpr float kIqLpfWbfm = 0.22f;
constexpr float kIqLpfNfm = 0.35f;
constexpr float kAudioLpfWbfm = 0.34f;
constexpr float kAudioLpfNfm = 0.55f;
constexpr float kDeemphWbfm = 0.2424f;  // 75 µs @ 48 kHz
constexpr float kDeemphNfm = 0.08f;

constexpr size_t kRingSamples = 48000;  // 1 s @ 48 kHz

int16_t* ring = nullptr;
size_t ring_cap = 0;
volatile size_t write_idx = 0;
volatile size_t read_idx = 0;
std::atomic_uint32_t seq_produced{0};
std::atomic_uint32_t underrun_count{0};
std::atomic_uint32_t overrun_count{0};
std::atomic<float> sig_dbfs{-90.0f};

float spectrum[kSpectrumBins]{};
portMUX_TYPE spectrum_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE ring_mux = portMUX_INITIALIZER_UNLOCKED;

struct DemodState {
  float iq_i_lpf = 0;
  float iq_q_lpf = 0;
  float i_sum = 0;
  float q_sum = 0;
  uint8_t rf_phase = 0;
  float prev_i = 0;
  float prev_q = 0;
  bool have_prev = false;
  float audio_lpf = 0;
  float audio_sum = 0;
  uint8_t audio_phase = 0;
  float deemph = 0;
  float dc = 0;
  float agc = 1.0f;
} st;

float fast_phase(float y, float x) {
  /* Cheap atan2-ish for discriminator (good enough for broadcast). */
  return atan2f(y, x);
}

void push_sample(int16_t s) {
  portENTER_CRITICAL(&ring_mux);
  const size_t next = (write_idx + 1) % ring_cap;
  if (next == read_idx) {
    /* Drop oldest. */
    read_idx = (read_idx + 1) % ring_cap;
    overrun_count.fetch_add(1, std::memory_order_relaxed);
  }
  ring[write_idx] = s;
  write_idx = next;
  portEXIT_CRITICAL(&ring_mux);
  seq_produced.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

bool begin() {
  if (ring) return true;
  ring = static_cast<int16_t*>(
      heap_caps_malloc(kRingSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!ring) {
    ring = static_cast<int16_t*>(
        heap_caps_malloc(kRingSamples * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (!ring) return false;
  ring_cap = kRingSamples;
  reset();
  return true;
}

void reset() {
  st = DemodState{};
  portENTER_CRITICAL(&ring_mux);
  write_idx = read_idx = 0;
  portEXIT_CRITICAL(&ring_mux);
  portENTER_CRITICAL(&spectrum_mux);
  std::memset(spectrum, 0, sizeof(spectrum));
  portEXIT_CRITICAL(&spectrum_mux);
  sig_dbfs.store(-90.0f, std::memory_order_relaxed);
}

void process_cu8(const uint8_t* iq, size_t bytes, bool wbfm) {
  if (!ring || !iq || bytes < 4) return;

  const float iq_k = wbfm ? kIqLpfWbfm : kIqLpfNfm;
  const float au_k = wbfm ? kAudioLpfWbfm : kAudioLpfNfm;
  const float de_k = wbfm ? kDeemphWbfm : kDeemphNfm;

  double power_acc = 0.0;
  size_t power_n = 0;
  float local_spec[kSpectrumBins]{};

  for (size_t off = 0; off + 1 < bytes; off += 2) {
    const float i_in = static_cast<float>(static_cast<int32_t>(iq[off]) - 128);
    const float q_in = static_cast<float>(static_cast<int32_t>(iq[off + 1]) - 128);
    power_acc += static_cast<double>(i_in * i_in + q_in * q_in);
    ++power_n;
    local_spec[(power_n * 3) % kSpectrumBins] += (i_in * i_in + q_in * q_in) * (1.0f / 16384.0f);

    st.iq_i_lpf += iq_k * (i_in - st.iq_i_lpf);
    st.iq_q_lpf += iq_k * (q_in - st.iq_q_lpf);
    st.i_sum += st.iq_i_lpf;
    st.q_sum += st.iq_q_lpf;
    if (++st.rf_phase != kRfDecim) continue;

    const float i = st.i_sum;
    const float q = st.q_sum;
    st.i_sum = 0;
    st.q_sum = 0;
    st.rf_phase = 0;

    if (!st.have_prev) {
      st.prev_i = i;
      st.prev_q = q;
      st.have_prev = true;
      continue;
    }

    /* Polar discriminator. */
    const float y = st.prev_i * q - st.prev_q * i;
    const float x = st.prev_i * i + st.prev_q * q;
    st.prev_i = i;
    st.prev_q = q;
    float phase = fast_phase(y, x);

    st.audio_lpf += au_k * (phase - st.audio_lpf);
    st.audio_sum += st.audio_lpf;
    if (++st.audio_phase != kAudioDecim) continue;

    float sample = st.audio_sum * (1.0f / static_cast<float>(kAudioDecim));
    st.audio_sum = 0;
    st.audio_phase = 0;

    /* De-emphasis. */
    st.deemph += de_k * (sample - st.deemph);
    sample = st.deemph;

    /* DC block. */
    st.dc += 0.0005f * (sample - st.dc);
    sample -= st.dc;

    /* Soft AGC toward ~0.25 peak. */
    const float env = fabsf(sample);
    st.agc += 0.0008f * ((env > 1e-4f ? 0.28f / env : st.agc) - st.agc);
    st.agc = std::clamp(st.agc, 0.05f, 40.0f);
    sample *= st.agc;
    sample = std::clamp(sample, -1.0f, 1.0f);

    push_sample(static_cast<int16_t>(sample * 30000.0f));
  }

  if (power_n > 0) {
    const float mean = static_cast<float>(power_acc / static_cast<double>(power_n) / 16384.0);
    sig_dbfs.store(10.0f * log10f(mean + 1e-12f), std::memory_order_relaxed);
  }
  portENTER_CRITICAL(&spectrum_mux);
  for (size_t i = 0; i < kSpectrumBins; ++i)
    spectrum[i] = spectrum[i] * 0.7f + local_spec[i] * 0.3f;
  portEXIT_CRITICAL(&spectrum_mux);
}

size_t pull_pcm(int16_t* out, size_t max_samples) {
  if (!ring || !out || max_samples == 0) return 0;
  size_t n = 0;
  portENTER_CRITICAL(&ring_mux);
  while (n < max_samples && read_idx != write_idx) {
    out[n++] = ring[read_idx];
    read_idx = (read_idx + 1) % ring_cap;
  }
  portEXIT_CRITICAL(&ring_mux);
  /* Do not count empty polls as underruns — browser polls faster than IQ. */
  return n;
}

uint32_t pcm_sequence() { return seq_produced.load(std::memory_order_relaxed); }

size_t pcm_available() {
  portENTER_CRITICAL(&ring_mux);
  const size_t w = write_idx, r = read_idx;
  portEXIT_CRITICAL(&ring_mux);
  if (w >= r) return w - r;
  return ring_cap - r + w;
}

float signal_dbfs() { return sig_dbfs.load(std::memory_order_relaxed); }

void copy_spectrum(float* out_bins, size_t count) {
  if (!out_bins || count == 0) return;
  portENTER_CRITICAL(&spectrum_mux);
  const size_t n = count < kSpectrumBins ? count : kSpectrumBins;
  std::memcpy(out_bins, spectrum, n * sizeof(float));
  portEXIT_CRITICAL(&spectrum_mux);
  for (size_t i = n; i < count; ++i) out_bins[i] = 0;
}

uint32_t underruns() { return underrun_count.load(std::memory_order_relaxed); }
uint32_t overruns() { return overrun_count.load(std::memory_order_relaxed); }

}  // namespace orcsdr::fm

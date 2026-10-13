#include "sstv_audio_scope.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace orcsdr::sstv_audio_scope {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr size_t kFirstBin = (static_cast<size_t>(kLowHz) * kFft) / kSampleRate;   // 29

struct State {
  int16_t window[kFft];
  size_t filled = 0;
  float hann[kFft];
  float real[kFft], imag[kFft];
  uint8_t rows[kRows * kBins];
  uint32_t sequence = 0;
};
State* g_state = nullptr;

void fft(float* re, float* im) {
  for (size_t i = 1, j = 0; i < kFft; ++i) {
    size_t bit = kFft >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
  }
  for (size_t len = 2; len <= kFft; len <<= 1) {
    const float angle = -2.0f * kPi / static_cast<float>(len);
    const float wr = std::cos(angle), wi = std::sin(angle);
    for (size_t base = 0; base < kFft; base += len) {
      float tr = 1.0f, ti = 0.0f;
      for (size_t k = 0; k < len / 2; ++k) {
        const size_t a = base + k, b = a + len / 2;
        const float pr = re[b] * tr - im[b] * ti, pi = re[b] * ti + im[b] * tr;
        re[b] = re[a] - pr; im[b] = im[a] - pi;
        re[a] += pr; im[a] += pi;
        const float nr = tr * wr - ti * wi;
        ti = tr * wi + ti * wr;
        tr = nr;
      }
    }
  }
}

void make_row() {
  State& s = *g_state;
  for (size_t i = 0; i < kFft; ++i) { s.real[i] = s.hann[i] * static_cast<float>(s.window[i]); s.imag[i] = 0.0f; }
  fft(s.real, s.imag);
  uint8_t* row = s.rows + static_cast<size_t>(s.sequence % kRows) * kBins;
  // A full-scale sine centred in a bin gives magnitude kFft/4 * 32768 with a Hann window; map -75..-15 dBFS to 0..255.
  const float norm = static_cast<float>(kFft) / 4.0f * 32768.0f;
  for (size_t b = 0; b < kBins; ++b) {
    const size_t k = kFirstBin + b;
    const float mag = std::sqrt(s.real[k] * s.real[k] + s.imag[k] * s.imag[k]) / norm;
    const float db = 20.0f * std::log10(mag + 1e-9f);
    row[b] = static_cast<uint8_t>(std::clamp((db + 75.0f) * (255.0f / 60.0f), 0.0f, 255.0f));
  }
  ++s.sequence;
}
}  // namespace

bool start() {
  if (g_state) return true;
#ifdef ESP_PLATFORM
  g_state = static_cast<State*>(heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  g_state = static_cast<State*>(std::calloc(1, sizeof(State)));
#endif
  if (!g_state) return false;
  for (size_t i = 0; i < kFft; ++i) g_state->hann[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * i / (kFft - 1));
  return true;
}

void stop() {
  if (!g_state) return;
#ifdef ESP_PLATFORM
  heap_caps_free(g_state);
#else
  std::free(g_state);
#endif
  g_state = nullptr;
}

bool active() { return g_state != nullptr; }

void push(const int16_t* samples, size_t count) {
  if (!g_state || !samples) return;
  State& s = *g_state;
  while (count) {
    const size_t take = std::min(count, kFft - s.filled);
    std::memcpy(s.window + s.filled, samples, take * sizeof(int16_t));
    s.filled += take;
    samples += take;
    count -= take;
    if (s.filled == kFft) {
      make_row();
      s.filled = 0;   // non-overlapping rows: kHop == kFft
    }
  }
}

View view() {
  View v;
  if (!g_state) return v;
  v.rows = g_state->rows;
  v.bins = kBins;
  v.row_count = kRows;
  v.sequence = g_state->sequence;
  return v;
}

uint32_t bin_hz(size_t bin) {
  return static_cast<uint32_t>(((kFirstBin + std::min(bin, kBins - 1)) * kSampleRate) / kFft + kSampleRate / (2 * kFft));
}

size_t bin_for_hz(uint32_t hz) {
  const int64_t k = (static_cast<int64_t>(hz) * kFft) / kSampleRate - static_cast<int64_t>(kFirstBin);
  return static_cast<size_t>(std::clamp<int64_t>(k, 0, static_cast<int64_t>(kBins) - 1));
}

bool self_check() {
  const uint32_t hz = bin_hz(bin_for_hz(1200));
  return kBins > 0 && hz + 24 >= 1200 && hz <= 1200 + 24 && bin_for_hz(100) == 0 && bin_for_hz(5000) == kBins - 1;
}

}  // namespace orcsdr::sstv_audio_scope

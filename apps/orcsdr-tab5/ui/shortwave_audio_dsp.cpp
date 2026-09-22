#include "shortwave_audio_dsp.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace orcsdr::shortwave::audio_dsp {
namespace {

constexpr float kSampleRate = 48000.0f;
constexpr size_t kToneWindow = 960;
constexpr size_t kToneBins = 25;

std::atomic<NoiseReduction> g_nr{NoiseReduction::off};
std::atomic<bool> g_notch_enabled{false};
std::atomic<SquelchMode> g_squelch{SquelchMode::off};
std::atomic<int> g_squelch_dbfs{-65};
std::atomic<bool> g_squelch_open{true};
std::atomic<bool> g_notch_active{false};
std::atomic<uint16_t> g_notch_hz{0};
std::atomic<float> g_input_rms_dbfs{-120.0f};
std::atomic<float> g_last_rf_dbfs{-90.0f};
std::atomic<bool> g_reset_requested{true};
std::atomic<bool> g_notch_reset_requested{false};
std::atomic<bool> g_auto_floor_reset_requested{false};

float g_low = 0.0f, g_voice = 0.0f;
std::array<float, 3> g_env{1.0f, 1.0f, 1.0f};
std::array<float, 3> g_noise{250.0f, 250.0f, 250.0f};
std::array<float, 3> g_gain{1.0f, 1.0f, 1.0f};
std::array<float, kToneBins> g_goertzel1{};
std::array<float, kToneBins> g_goertzel2{};
size_t g_tone_samples = 0;
int g_candidate_bin = -1, g_candidate_frames = 0, g_missed_frames = 0;
float g_notch_x1 = 0.0f, g_notch_x2 = 0.0f, g_notch_y1 = 0.0f,
      g_notch_y2 = 0.0f;
float g_notch_cosine = 0.0f;
float g_gate = 1.0f;
float g_auto_floor = -90.0f;
uint32_t g_squelch_hang = 0;

float tone_frequency(size_t bin) { return 250.0f + 125.0f * bin; }
float tone_coefficient(size_t bin) {
  static const std::array<float, kToneBins> coefficients = [] {
    std::array<float, kToneBins> values{};
    for (size_t i = 0; i < values.size(); ++i)
      values[i] = 2.0f * std::cos(2.0f * 3.14159265358979323846f *
                                  tone_frequency(i) / kSampleRate);
    return values;
  }();
  return coefficients[bin];
}

void reset_notch_state() {
  g_goertzel1.fill(0.0f);
  g_goertzel2.fill(0.0f);
  g_tone_samples = 0;
  g_candidate_bin = -1;
  g_candidate_frames = 0;
  g_missed_frames = 0;
  g_notch_x1 = g_notch_x2 = g_notch_y1 = g_notch_y2 = 0.0f;
  g_notch_active.store(false, std::memory_order_relaxed);
  g_notch_hz.store(0, std::memory_order_relaxed);
}

void reset_processor_state() {
  g_low = g_voice = 0.0f;
  g_env = {1.0f, 1.0f, 1.0f};
  g_noise = {250.0f, 250.0f, 250.0f};
  g_gain = {1.0f, 1.0f, 1.0f};
  g_gate = 1.0f;
  g_squelch_hang = 0;
  reset_notch_state();
}

void finish_tone_window() {
  float total = 0.0f, peak = 0.0f;
  int best = -1;
  for (size_t bin = 0; bin < kToneBins; ++bin) {
    const float power = g_goertzel1[bin] * g_goertzel1[bin] +
        g_goertzel2[bin] * g_goertzel2[bin] -
        tone_coefficient(bin) * g_goertzel1[bin] * g_goertzel2[bin];
    total += power;
    if (power > peak) {
      peak = power;
      best = static_cast<int>(bin);
    }
  }
  const bool tonal = best >= 0 && peak > 1.0e7f && peak * 3.0f > total;
  if (tonal) {
    if (std::abs(best - g_candidate_bin) <= 1)
      ++g_candidate_frames;
    else {
      g_candidate_bin = best;
      g_candidate_frames = 1;
    }
    g_missed_frames = 0;
    if (g_candidate_frames >= 4) {
      const uint16_t hz = static_cast<uint16_t>(tone_frequency(best));
      if (hz != g_notch_hz.load(std::memory_order_relaxed))
        g_notch_x1 = g_notch_x2 = g_notch_y1 = g_notch_y2 = 0.0f;
      g_notch_cosine = std::cos(2.0f * 3.14159265358979323846f * hz /
                                kSampleRate);
      g_notch_hz.store(hz, std::memory_order_relaxed);
      g_notch_active.store(true, std::memory_order_relaxed);
    }
  } else if (++g_missed_frames >= 20) {
    g_notch_active.store(false, std::memory_order_relaxed);
    g_notch_hz.store(0, std::memory_order_relaxed);
  }
  g_goertzel1.fill(0.0f);
  g_goertzel2.fill(0.0f);
  g_tone_samples = 0;
}

float track_and_notch(float sample) {
  for (size_t bin = 0; bin < kToneBins; ++bin) {
    const float next = sample + tone_coefficient(bin) * g_goertzel1[bin] -
                       g_goertzel2[bin];
    g_goertzel2[bin] = g_goertzel1[bin];
    g_goertzel1[bin] = next;
  }
  if (++g_tone_samples == kToneWindow) finish_tone_window();
  if (!g_notch_active.load(std::memory_order_relaxed)) return sample;
  constexpr float radius = 0.985f;
  const float output = sample - 2.0f * g_notch_cosine * g_notch_x1 + g_notch_x2 +
                       2.0f * radius * g_notch_cosine * g_notch_y1 -
                       radius * radius * g_notch_y2;
  g_notch_x2 = g_notch_x1;
  g_notch_x1 = sample;
  g_notch_y2 = g_notch_y1;
  g_notch_y1 = output;
  return output;
}

float reduce_noise(float sample, NoiseReduction mode) {
  if (mode == NoiseReduction::off) return sample;
  constexpr float low_alpha = 0.10f;
  constexpr float voice_alpha = 0.36f;
  g_low += low_alpha * (sample - g_low);
  g_voice += voice_alpha * (sample - g_voice);
  const std::array<float, 3> bands{g_low, g_voice - g_low, sample - g_voice};
  const std::array<float, 3> floor = mode == NoiseReduction::low
      ? std::array<float, 3>{0.82f, 0.58f, 0.30f}
      : std::array<float, 3>{0.68f, 0.38f, 0.14f};
  float output = 0.0f;
  for (size_t band = 0; band < bands.size(); ++band) {
    const float magnitude = std::fabs(bands[band]);
    g_env[band] += (magnitude > g_env[band] ? 0.04f : 0.002f) *
                   (magnitude - g_env[band]);
    const float noise_rate = g_env[band] < g_noise[band] ? 0.015f : 0.00002f;
    g_noise[band] += noise_rate * (g_env[band] - g_noise[band]);
    const float ratio = (g_env[band] + 1.0f) / (g_noise[band] + 1.0f);
    const float activity = std::clamp((ratio - 1.15f) / 1.85f, 0.0f, 1.0f);
    const float target = floor[band] + (1.0f - floor[band]) * activity;
    g_gain[band] += 0.015f * (target - g_gain[band]);
    output += bands[band] * g_gain[band];
  }
  return output;
}

void update_squelch(float rf_dbfs, size_t count) {
  const SquelchMode mode = g_squelch.load(std::memory_order_relaxed);
  if (mode == SquelchMode::off) {
    g_squelch_open.store(true, std::memory_order_relaxed);
    g_squelch_hang = 24000;
    return;
  }
  float threshold = static_cast<float>(g_squelch_dbfs.load(std::memory_order_relaxed));
  if (mode == SquelchMode::automatic) {
    if (!g_squelch_open.load(std::memory_order_relaxed))
      g_auto_floor += 0.05f * (rf_dbfs - g_auto_floor);
    threshold = g_auto_floor + 6.0f;
  }
  bool open = g_squelch_open.load(std::memory_order_relaxed);
  if (rf_dbfs >= threshold) {
    open = true;
    g_squelch_hang = 24000;
  } else if (g_squelch_hang > count) {
    g_squelch_hang -= static_cast<uint32_t>(count);
  } else if (rf_dbfs < threshold - 2.0f) {
    open = false;
    g_squelch_hang = 0;
  }
  g_squelch_open.store(open, std::memory_order_relaxed);
}

}  // namespace

Settings settings() {
  return {g_nr.load(std::memory_order_relaxed),
          g_notch_enabled.load(std::memory_order_relaxed),
          g_squelch.load(std::memory_order_relaxed),
          static_cast<int8_t>(g_squelch_dbfs.load(std::memory_order_relaxed))};
}

Metrics metrics() {
  return {g_squelch_open.load(std::memory_order_relaxed),
          g_notch_active.load(std::memory_order_relaxed),
          g_notch_hz.load(std::memory_order_relaxed),
          g_input_rms_dbfs.load(std::memory_order_relaxed)};
}

void reset() {
  g_reset_requested.store(true, std::memory_order_release);
}

void cycle_noise_reduction() {
  const auto current = g_nr.load(std::memory_order_relaxed);
  g_nr.store(current == NoiseReduction::off ? NoiseReduction::low
             : current == NoiseReduction::low ? NoiseReduction::high
                                              : NoiseReduction::off,
             std::memory_order_relaxed);
}

void toggle_auto_notch() {
  const bool enabled = !g_notch_enabled.load(std::memory_order_relaxed);
  g_notch_enabled.store(enabled, std::memory_order_relaxed);
  if (!enabled) g_notch_reset_requested.store(true, std::memory_order_release);
}

void cycle_squelch() {
  const auto current = g_squelch.load(std::memory_order_relaxed);
  const auto next = current == SquelchMode::off ? SquelchMode::automatic
                    : current == SquelchMode::automatic ? SquelchMode::manual
                                                        : SquelchMode::off;
  if (next == SquelchMode::automatic)
    g_auto_floor_reset_requested.store(true, std::memory_order_release);
  g_squelch.store(next, std::memory_order_relaxed);
}

void adjust_squelch(int delta_db) {
  g_squelch_dbfs.store(
      std::clamp(g_squelch_dbfs.load(std::memory_order_relaxed) + delta_db,
                 -100, -20),
      std::memory_order_relaxed);
  g_squelch.store(SquelchMode::manual, std::memory_order_relaxed);
}

void apply_clean_preset() {
  g_nr.store(NoiseReduction::low, std::memory_order_relaxed);
  g_notch_enabled.store(true, std::memory_order_relaxed);
  g_squelch.store(SquelchMode::off, std::memory_order_relaxed);
}

void process(int16_t* samples, size_t count, float rf_dbfs) {
  if (!samples || !count) return;
  if (g_reset_requested.exchange(false, std::memory_order_acq_rel))
    reset_processor_state();
  else if (g_notch_reset_requested.exchange(false, std::memory_order_acq_rel))
    reset_notch_state();
  g_last_rf_dbfs.store(rf_dbfs, std::memory_order_relaxed);
  if (g_auto_floor_reset_requested.exchange(false, std::memory_order_acq_rel))
    g_auto_floor = rf_dbfs;
  double squares = 0.0;
  for (size_t i = 0; i < count; ++i)
    squares += static_cast<double>(samples[i]) * samples[i];
  const float rms = std::sqrt(static_cast<float>(squares / count));
  g_input_rms_dbfs.store(20.0f * std::log10(rms / 32768.0f + 1.0e-9f),
                         std::memory_order_relaxed);
  update_squelch(rf_dbfs, count);
  const auto nr = g_nr.load(std::memory_order_relaxed);
  const bool notch = g_notch_enabled.load(std::memory_order_relaxed);
  const float gate_target = g_squelch_open.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
  for (size_t i = 0; i < count; ++i) {
    float value = reduce_noise(static_cast<float>(samples[i]), nr);
    if (notch) value = track_and_notch(value);
    g_gate += 0.0025f * (gate_target - g_gate);
    samples[i] = static_cast<int16_t>(std::clamp(
        static_cast<int>(std::lround(value * g_gate)), -32768, 32767));
  }
}

}  // namespace orcsdr::shortwave::audio_dsp

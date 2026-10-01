#include "airband_audio_filter.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace orcsdr::airband_audio {
namespace {

// Direct form II transposed biquad, coefficients normalised so a0 == 1.
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1 = 0, z2 = 0;

  float step(float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

constexpr float kPi = 3.14159265358979323846f;
constexpr float kQ = 0.70710678f;   // Butterworth section

Biquad low_pass(float hz) {
  const float w0 = 2.0f * kPi * hz / kSampleRateHz;
  const float alpha = std::sin(w0) / (2.0f * kQ);
  const float c = std::cos(w0);
  const float a0 = 1.0f + alpha;
  Biquad q;
  q.b0 = (1.0f - c) * 0.5f / a0;
  q.b1 = (1.0f - c) / a0;
  q.b2 = q.b0;
  q.a1 = -2.0f * c / a0;
  q.a2 = (1.0f - alpha) / a0;
  return q;
}

Biquad high_pass(float hz) {
  const float w0 = 2.0f * kPi * hz / kSampleRateHz;
  const float alpha = std::sin(w0) / (2.0f * kQ);
  const float c = std::cos(w0);
  const float a0 = 1.0f + alpha;
  Biquad q;
  q.b0 = (1.0f + c) * 0.5f / a0;
  q.b1 = -(1.0f + c) / a0;
  q.b2 = q.b0;
  q.a1 = -2.0f * c / a0;
  q.a2 = (1.0f - alpha) / a0;
  return q;
}

// Two-pole high-pass, then a four-pole low-pass (two cascaded sections) for a steeper roll-off
// above speech, where the hiss lives.
Biquad g_high = high_pass(kHighPassHz);
Biquad g_low_a = low_pass(kLowPassHz);
Biquad g_low_b = low_pass(kLowPassHz);

float magnitude(const Biquad& q, float hz) {
  const float w = 2.0f * kPi * hz / kSampleRateHz;
  const std::complex<float> z1 = std::polar(1.0f, -w);
  const std::complex<float> z2 = std::polar(1.0f, -2.0f * w);
  const std::complex<float> num = q.b0 + q.b1 * z1 + q.b2 * z2;
  const std::complex<float> den = 1.0f + q.a1 * z1 + q.a2 * z2;
  return std::abs(num / den);
}

}  // namespace

void reset() {
  g_high.z1 = g_high.z2 = 0;
  g_low_a.z1 = g_low_a.z2 = 0;
  g_low_b.z1 = g_low_b.z2 = 0;
}

void process(int16_t* samples, size_t count) {
  if (samples == nullptr) return;
  for (size_t i = 0; i < count; ++i) {
    float x = static_cast<float>(samples[i]);
    x = g_high.step(x);
    x = g_low_a.step(x);
    x = g_low_b.step(x);
    samples[i] = static_cast<int16_t>(std::clamp(x, -32768.0f, 32767.0f));
  }
}

float response(float frequency_hz) {
  return magnitude(g_high, frequency_hz) * magnitude(g_low_a, frequency_hz) *
         magnitude(g_low_b, frequency_hz);
}

}  // namespace orcsdr::airband_audio

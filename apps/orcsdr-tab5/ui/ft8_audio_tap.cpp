#include "ft8_audio_tap.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orcsdr::ftx::audio_tap {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCenterHz = 1600.0;  // middle of the 200-3000 Hz USB passband

double bessel_i0(double x) {
  double sum = 1.0, term = 1.0;
  const double q = x * x / 4.0;
  for (int k = 1; k < 60; ++k) {
    term *= q / (static_cast<double>(k) * static_cast<double>(k));
    sum += term;
    if (term < sum * 1e-15) break;
  }
  return sum;
}

// Linear-phase Kaiser-windowed-sinc low-pass; `cutoff` is the -6 dB edge in cycles/sample. Unity DC gain.
template <size_t N>
void design_lowpass(std::array<float, N>* taps, double cutoff, double beta) {
  const double mid = (static_cast<double>(N) - 1.0) / 2.0;
  const double i0_beta = bessel_i0(beta);
  double sum = 0.0;
  double h[N];
  for (size_t n = 0; n < N; ++n) {
    const double m = static_cast<double>(n) - mid;
    const double sinc = (m == 0.0) ? 2.0 * cutoff : std::sin(2.0 * kPi * cutoff * m) / (kPi * m);
    const double r = m / mid;
    const double window = bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0_beta;
    h[n] = sinc * window;
    sum += h[n];
  }
  for (size_t n = 0; n < N; ++n) (*taps)[n] = static_cast<float>(h[n] / sum);
}

inline void rotate(double* c, double* s, double step_cos, double step_sin) {
  const double nc = *c * step_cos - *s * step_sin;
  *s = *s * step_cos + *c * step_sin;
  *c = nc;
}

inline void renormalize(double* c, double* s) {
  const double n = std::sqrt(*c * *c + *s * *s);
  if (n > 0.0) {
    *c /= n;
    *s /= n;
  }
}

}  // namespace

bool rate_supported(uint32_t input_rate_hz) {
  return input_rate_hz >= kBaseRateHz && input_rate_hz % kBaseRateHz == 0 && input_rate_hz / kBaseRateHz <= 64;
}

bool begin(Tap* tap, uint32_t input_rate_hz) {
  if (tap == nullptr || !rate_supported(input_rate_hz)) return false;
  *tap = Tap{};
  tap->input_rate_hz = input_rate_hz;
  tap->cic_ratio = input_rate_hz / kBaseRateHz;
  const double r = static_cast<double>(tap->cic_ratio);
  tap->inv_cic_gain = static_cast<float>(1.0 / (r * r * r));
  // Stage A: /5 at 240 kS/s. Anything below 46 kHz that is not rejected here only aliases to harmless frequencies; the
  // sharp rejection is in stage B.
  design_lowpass(&tap->taps_a, 22000.0 / kBaseRateHz, 6.0);
  // Stage B: /4 at 48 kS/s. -6 dB at 1675 Hz, between the +1450 Hz passband edge and the 1900 Hz opposite-sideband edge.
  design_lowpass(&tap->taps_b, 1675.0 / 48000.0, 6.8);
  reset(tap);
  return true;
}

void reset(Tap* tap) {
  if (tap == nullptr) return;
  std::memset(tap->integ, 0, sizeof(tap->integ));
  std::memset(tap->comb, 0, sizeof(tap->comb));
  tap->cic_phase = 0;
  tap->dc[0] = tap->dc[1] = 0.0f;
  tap->mix1_cos = 1.0;
  tap->mix1_sin = 0.0;
  tap->mix2_cos = 1.0;
  tap->mix2_sin = 0.0;
  tap->mix_renorm = 0;
  for (int ch = 0; ch < 2; ++ch) {
    tap->hist_a[ch].fill(0.0f);
    tap->hist_b[ch].fill(0.0f);
  }
  tap->head_a = tap->head_b = 0;
  tap->phase_a = tap->phase_b = 0;
  tap->metrics = Metrics{};
}

size_t max_output_samples(const Tap& tap, size_t bytes) {
  if (tap.cic_ratio == 0) return 0;
  const size_t complex_samples = bytes / 2;
  return complex_samples / (static_cast<size_t>(tap.cic_ratio) * 20u) + 2;
}

size_t process_cu8(Tap* tap, const uint8_t* iq, size_t bytes, int16_t* out, size_t capacity) {
  if (tap == nullptr || tap->cic_ratio == 0 || iq == nullptr || out == nullptr) return 0;
  const double step1 = -2.0 * kPi * kCenterHz / static_cast<double>(kBaseRateHz);
  const double step2 = 2.0 * kPi * kCenterHz / static_cast<double>(kOutputRateHz);
  const double c1 = std::cos(step1), s1 = std::sin(step1);
  const double c2 = std::cos(step2), s2 = std::sin(step2);

  size_t written = 0;
  const size_t pairs = bytes / 2;
  tap->metrics.input_samples += pairs;
  ++tap->metrics.blocks;

  for (size_t n = 0; n < pairs; ++n) {
    // ---- CIC3 decimation (I and Q), integer arithmetic
    int32_t in[2] = {static_cast<int32_t>(iq[2 * n]) - 128, static_cast<int32_t>(iq[2 * n + 1]) - 128};
    for (int ch = 0; ch < 2; ++ch) {
      uint32_t v = static_cast<uint32_t>(in[ch]);
      tap->integ[ch][0] = static_cast<int32_t>(static_cast<uint32_t>(tap->integ[ch][0]) + v);
      tap->integ[ch][1] = static_cast<int32_t>(static_cast<uint32_t>(tap->integ[ch][1]) +
                                               static_cast<uint32_t>(tap->integ[ch][0]));
      tap->integ[ch][2] = static_cast<int32_t>(static_cast<uint32_t>(tap->integ[ch][2]) +
                                               static_cast<uint32_t>(tap->integ[ch][1]));
    }
    if (++tap->cic_phase < tap->cic_ratio) continue;
    tap->cic_phase = 0;

    float x[2];
    for (int ch = 0; ch < 2; ++ch) {
      uint32_t v = static_cast<uint32_t>(tap->integ[ch][2]);
      for (int k = 0; k < 3; ++k) {
        const uint32_t d = v - static_cast<uint32_t>(tap->comb[ch][k]);
        tap->comb[ch][k] = static_cast<int32_t>(v);
        v = d;
      }
      x[ch] = static_cast<float>(static_cast<int32_t>(v)) * tap->inv_cic_gain;
      // ---- DC removal (the dongle's DC offset; corner about 19 Hz at 240 kS/s)
      tap->dc[ch] += 0.0005f * (x[ch] - tap->dc[ch]);
      x[ch] -= tap->dc[ch];
    }

    // ---- mix the USB passband centre to 0 Hz
    const float mc = static_cast<float>(tap->mix1_cos), ms = static_cast<float>(tap->mix1_sin);
    const float zi = x[0] * mc - x[1] * ms;
    const float zq = x[0] * ms + x[1] * mc;
    rotate(&tap->mix1_cos, &tap->mix1_sin, c1, s1);
    if (++tap->mix_renorm >= 65536u) {
      tap->mix_renorm = 0;
      renormalize(&tap->mix1_cos, &tap->mix1_sin);
    }

    // ---- stage A: FIR /5
    tap->hist_a[0][tap->head_a] = zi;
    tap->hist_a[1][tap->head_a] = zq;
    const size_t pos_a = tap->head_a;
    tap->head_a = (tap->head_a + 1) % kStageATaps;
    if (++tap->phase_a < 5) continue;
    tap->phase_a = 0;
    float ai = 0.0f, aq = 0.0f;
    for (size_t k = 0; k < kStageATaps; ++k) {
      const size_t idx = (pos_a + kStageATaps - k) % kStageATaps;
      ai += tap->taps_a[k] * tap->hist_a[0][idx];
      aq += tap->taps_a[k] * tap->hist_a[1][idx];
    }

    // ---- stage B: FIR /4
    tap->hist_b[0][tap->head_b] = ai;
    tap->hist_b[1][tap->head_b] = aq;
    const size_t pos_b = tap->head_b;
    tap->head_b = (tap->head_b + 1) % kStageBTaps;
    if (++tap->phase_b < 4) continue;
    tap->phase_b = 0;
    float bi = 0.0f, bq = 0.0f;
    size_t idx = pos_b;
    for (size_t k = 0; k < kStageBTaps; ++k) {
      bi += tap->taps_b[k] * tap->hist_b[0][idx];
      bq += tap->taps_b[k] * tap->hist_b[1][idx];
      idx = (idx == 0) ? kStageBTaps - 1 : idx - 1;
    }

    // ---- mix back up by the centre frequency and take the real part: 12 kS/s USB audio
    const float uc = static_cast<float>(tap->mix2_cos), us = static_cast<float>(tap->mix2_sin);
    const float audio = (bi * uc - bq * us) * kOutputScale;
    rotate(&tap->mix2_cos, &tap->mix2_sin, c2, s2);
    float v = audio;
    if (v > 32767.0f) { v = 32767.0f; ++tap->metrics.clipped_outputs; }
    if (v < -32768.0f) { v = -32768.0f; ++tap->metrics.clipped_outputs; }
    if (written < capacity) {
      out[written++] = static_cast<int16_t>(std::lrintf(v));
      ++tap->metrics.output_samples;
    }
  }
  return written;
}

bool self_check() {
  Tap tap;
  return begin(&tap, 2400000) && tap.cic_ratio == 10 && !rate_supported(2048000) && !rate_supported(100000) &&
         rate_supported(240000) && max_output_samples(tap, 4800000) >= 12000;
}

}  // namespace orcsdr::ftx::audio_tap

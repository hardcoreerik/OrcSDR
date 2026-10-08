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

inline void rotate(float* c, float* s, float step_cos, float step_sin) {
  const float nc = *c * step_cos - *s * step_sin;
  *s = *s * step_cos + *c * step_sin;
  *c = nc;
}

inline void renormalize(float* c, float* s) {
  const float n = std::sqrt(*c * *c + *s * *s);
  if (n > 0.0f) {
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

void set_dial_offset(Tap* tap, float dial_offset_hz) {
  if (tap != nullptr && std::isfinite(dial_offset_hz) && std::fabs(dial_offset_hz) < 20000.0f) tap->dial_offset_hz = dial_offset_hz;
}

void reset(Tap* tap) {
  if (tap == nullptr) return;
  std::memset(tap->integ, 0, sizeof(tap->integ));
  std::memset(tap->comb, 0, sizeof(tap->comb));
  tap->cic_phase = 0;
  tap->dc[0] = tap->dc[1] = 0.0f;
  tap->mix1_cos = 1.0f;
  tap->mix1_sin = 0.0f;
  tap->mix2_cos = 1.0f;
  tap->mix2_sin = 0.0f;
  tap->mix1_count = 0;
  tap->mix2_count = 0;
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
  const double mix1_hz = static_cast<double>(tap->dial_offset_hz) + kCenterHz;   // the USB passband centre in the baseband
  const float c1 = static_cast<float>(std::cos(-2.0 * kPi * mix1_hz / static_cast<double>(kBaseRateHz)));
  const float s1 = static_cast<float>(std::sin(-2.0 * kPi * mix1_hz / static_cast<double>(kBaseRateHz)));
  const float c2 = static_cast<float>(std::cos(2.0 * kPi * kCenterHz / static_cast<double>(kOutputRateHz)));
  const float s2 = static_cast<float>(std::sin(2.0 * kPi * kCenterHz / static_cast<double>(kOutputRateHz)));

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
    const float zi = x[0] * tap->mix1_cos - x[1] * tap->mix1_sin;
    const float zq = x[0] * tap->mix1_sin + x[1] * tap->mix1_cos;
    rotate(&tap->mix1_cos, &tap->mix1_sin, c1, s1);
    if (++tap->mix1_count >= 2048u) {
      tap->mix1_count = 0;
      renormalize(&tap->mix1_cos, &tap->mix1_sin);
    }

    // ---- stage A: FIR /5 (double-length history: the newest sample is at pos + N, older ones at lower addresses)
    {
      const size_t p = tap->head_a;
      tap->hist_a[0][p] = tap->hist_a[0][p + kStageATaps] = zi;
      tap->hist_a[1][p] = tap->hist_a[1][p + kStageATaps] = zq;
      tap->head_a = (p + 1 == kStageATaps) ? 0 : p + 1;
    }
    if (++tap->phase_a < 5) continue;
    tap->phase_a = 0;
    float ai = 0.0f, aq = 0.0f;
    {
      const size_t newest = (tap->head_a == 0 ? kStageATaps : tap->head_a) - 1 + kStageATaps;
      const float* hi = &tap->hist_a[0][newest];
      const float* hq = &tap->hist_a[1][newest];
      for (size_t k = 0; k < kStageATaps; ++k) {
        ai += tap->taps_a[k] * hi[-static_cast<long>(k)];
        aq += tap->taps_a[k] * hq[-static_cast<long>(k)];
      }
    }

    // ---- stage B: FIR /4
    {
      const size_t p = tap->head_b;
      tap->hist_b[0][p] = tap->hist_b[0][p + kStageBTaps] = ai;
      tap->hist_b[1][p] = tap->hist_b[1][p + kStageBTaps] = aq;
      tap->head_b = (p + 1 == kStageBTaps) ? 0 : p + 1;
    }
    if (++tap->phase_b < 4) continue;
    tap->phase_b = 0;
    float bi = 0.0f, bq = 0.0f;
    {
      const size_t newest = (tap->head_b == 0 ? kStageBTaps : tap->head_b) - 1 + kStageBTaps;
      const float* hi = &tap->hist_b[0][newest];
      const float* hq = &tap->hist_b[1][newest];
      for (size_t k = 0; k < kStageBTaps; ++k) {
        bi += tap->taps_b[k] * hi[-static_cast<long>(k)];
        bq += tap->taps_b[k] * hq[-static_cast<long>(k)];
      }
    }

    // ---- mix back up by the centre frequency and take the real part: 12 kS/s USB audio
    const float audio = (bi * tap->mix2_cos - bq * tap->mix2_sin) * kOutputScale;
    rotate(&tap->mix2_cos, &tap->mix2_sin, c2, s2);
    if (++tap->mix2_count >= 2048u) {
      tap->mix2_count = 0;
      renormalize(&tap->mix2_cos, &tap->mix2_sin);
    }
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

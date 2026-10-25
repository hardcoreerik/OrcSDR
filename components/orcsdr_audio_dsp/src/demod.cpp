#include "orcsdr_dsp/demod.hpp"
#include <algorithm>
#include <cmath>

namespace orcsdr::dsp {
namespace {
constexpr float kPi = 3.14159265358979323846f;

// Windowed-sinc low-pass (Blackman), unity DC gain. cutoff is a fraction of the sample rate.
std::vector<float> lowpass(size_t taps, float cutoff) {
  std::vector<float> h(taps);
  const float mid = (taps - 1) / 2.0f;
  float sum = 0;
  for (size_t n = 0; n < taps; ++n) {
    const float x = n - mid;
    const float sinc = x == 0 ? 2 * cutoff : std::sin(2 * kPi * cutoff * x) / (kPi * x);
    const float w = 0.42f - 0.5f * std::cos(2 * kPi * n / (taps - 1)) + 0.08f * std::cos(4 * kPi * n / (taps - 1));
    h[n] = sinc * w;
    sum += h[n];
  }
  for (float& v : h) v /= sum;
  return h;
}
}  // namespace

float fast_atan2(float y, float x) {
  const float ax = std::fabs(x), ay = std::fabs(y);
  const float hi = std::max(ax, ay), lo = std::min(ax, ay);
  if (hi == 0) return 0;
  const float a = lo / hi, s = a * a;
  float r = a * (0.9998660f + s * (-0.3302995f + s * (0.1801410f + s * (-0.0851330f + s * 0.0208351f))));
  if (ay > ax) r = kPi / 2 - r;
  if (x < 0) r = kPi - r;
  return y < 0 ? -r : r;
}

void Demod::ComplexFir::init(const std::vector<float>& t, unsigned d) {
  taps = t;
  decim = d;
  hist_i.assign(2 * taps.size(), 0.0f);
  hist_q.assign(2 * taps.size(), 0.0f);
  pos = 0;
  phase = 0;
}
void Demod::ComplexFir::reset() {
  std::fill(hist_i.begin(), hist_i.end(), 0.0f);
  std::fill(hist_q.begin(), hist_q.end(), 0.0f);
  pos = 0;
  phase = 0;
}
bool Demod::ComplexFir::push(float i, float q, float* oi, float* oq) {
  const size_t n = taps.size();
  hist_i[pos] = hist_i[pos + n] = i;
  hist_q[pos] = hist_q[pos + n] = q;
  pos = pos + 1 == n ? 0 : pos + 1;
  if (++phase < decim) return false;
  phase = 0;
  // Newest sample is at pos-1; taps[0] multiplies the newest.
  const float* hi = &hist_i[pos];
  const float* hq = &hist_q[pos];
  float ai = 0, aq = 0;
  for (size_t k = 0; k < n; ++k) {
    ai += taps[k] * hi[n - 1 - k];
    aq += taps[k] * hq[n - 1 - k];
  }
  *oi = ai;
  *oq = aq;
  return true;
}

void Demod::RealFir::init(const std::vector<float>& t, unsigned d) {
  taps = t;
  decim = d;
  hist.assign(2 * taps.size(), 0.0f);
  pos = 0;
  phase = 0;
}
void Demod::RealFir::reset() {
  std::fill(hist.begin(), hist.end(), 0.0f);
  pos = 0;
  phase = 0;
}
bool Demod::RealFir::push(float x, float* out) {
  const size_t n = taps.size();
  hist[pos] = hist[pos + n] = x;
  pos = pos + 1 == n ? 0 : pos + 1;
  if (++phase < decim) return false;
  phase = 0;
  const float* h = &hist[pos];
  float acc = 0;
  for (size_t k = 0; k < n; ++k) acc += taps[k] * h[n - 1 - k];
  *out = acc;
  return true;
}

Demod::Demod() {
  stage_a_.init(lowpass(33, 100000.0f / kInputRate), 4);
  stage_b_nb_.init(lowpass(65, 7500.0f / 240000.0f), 5);
  stage_b_wfm_.init(lowpass(65, 15000.0f / 240000.0f), 5);
}

void Demod::set_mode(Mode mode) {
  if (mode == mode_) return;
  mode_ = mode;
  reset();
}

void Demod::reset() {
  stage_a_.reset();
  stage_b_nb_.reset();
  stage_b_wfm_.reset();
  prev_i_ = prev_q_ = deemph_ = dc_ = nfm_lp_ = 0;
  level_ = 0.1f;
}

size_t Demod::process(const uint8_t* iq, size_t bytes, float* out, size_t capacity) {
  constexpr float kScale = 1.0f / 127.5f;
  constexpr float kWfmGain = 240000.0f / (2 * kPi * 75000.0f);   // +/-75 kHz deviation -> +/-1
  constexpr float kNfmGain = 48000.0f / (2 * kPi * 5000.0f);     // +/-5 kHz deviation -> +/-1
  const float deemph_alpha = 1.0f - std::exp(-1.0f / (48000.0f * 75e-6f));
  size_t written = 0;
  for (size_t s = 0; s + 1 < bytes; s += 2) {
    float ai, aq;
    if (!stage_a_.push((iq[s] - 127.5f) * kScale, (iq[s + 1] - 127.5f) * kScale, &ai, &aq)) continue;
    if (mode_ == Mode::wfm) {
      // Discriminator at 240 kS/s: phase advance between consecutive samples.
      const float re = ai * prev_i_ + aq * prev_q_;
      const float im = aq * prev_i_ - ai * prev_q_;
      prev_i_ = ai;
      prev_q_ = aq;
      float audio;
      if (!stage_b_wfm_.push(fast_atan2(im, re) * kWfmGain, &audio)) continue;
      deemph_ += deemph_alpha * (audio - deemph_);
      if (written < capacity) out[written++] = std::clamp(deemph_, -1.0f, 1.0f);
      continue;
    }
    float bi, bq;
    if (!stage_b_nb_.push(ai, aq, &bi, &bq)) continue;
    float audio;
    if (mode_ == Mode::nfm) {
      const float re = bi * prev_i_ + bq * prev_q_;
      const float im = bq * prev_i_ - bi * prev_q_;
      prev_i_ = bi;
      prev_q_ = bq;
      nfm_lp_ += 0.6f * (fast_atan2(im, re) * kNfmGain - nfm_lp_);  // gentle smoothing of the hiss
      audio = nfm_lp_;
    } else {
      const float env = std::sqrt(bi * bi + bq * bq);
      dc_ += 0.0005f * (env - dc_);        // carrier level
      level_ += 0.0002f * (dc_ - level_);  // slow AGC reference
      audio = (env - dc_) / std::max(level_, 0.02f) * 0.5f;
    }
    if (written < capacity) out[written++] = std::clamp(audio, -1.0f, 1.0f);
  }
  return written;
}

}  // namespace orcsdr::dsp

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
  const std::vector<float> a = lowpass(kATaps, 100000.0f / kInputRate);
  std::copy(a.begin(), a.end(), a_taps_);
  xi_.assign(kATaps - 1 + kChunk, 0.0f);
  xq_.assign(kATaps - 1 + kChunk, 0.0f);
  stage_b_nb_.init(lowpass(65, 7500.0f / 240000.0f), 5);
  stage_b_wfm_.init(lowpass(65, 15000.0f / 240000.0f), 5);
}

void Demod::set_mode(Mode mode) {
  if (mode == mode_) return;
  mode_ = mode;
  reset();
}

void Demod::reset() {
  std::fill(a_hist_i_, a_hist_i_ + kATaps - 1, 0.0f);
  std::fill(a_hist_q_, a_hist_q_ + kATaps - 1, 0.0f);
  a_phase_ = 0;
  stage_b_nb_.reset();
  stage_b_wfm_.reset();
  prev_i_ = prev_q_ = deemph_ = dc_ = nfm_lp_ = 0;
  level_ = 0.1f;
}

size_t Demod::process(const uint8_t* iq, size_t bytes, float* out, size_t capacity) {
  constexpr float kScale = 1.0f / 127.5f;
  size_t written = 0;
  constexpr size_t kH = kATaps - 1;
  size_t pairs_left = bytes / 2;
  const uint8_t* src = iq;
  while (pairs_left) {
    const size_t n = std::min(pairs_left, kChunk);
    pairs_left -= n;
    float* xi = xi_.data();
    float* xq = xq_.data();
    std::copy(a_hist_i_, a_hist_i_ + kH, xi);
    std::copy(a_hist_q_, a_hist_q_ + kH, xq);
    for (size_t k = 0; k < n; ++k) {
      xi[kH + k] = (src[2 * k] - 127.5f) * kScale;
      xq[kH + k] = (src[2 * k + 1] - 127.5f) * kScale;
    }
    src += 2 * n;
    const size_t first = 3 - a_phase_;  // index of the first output in this chunk
    for (size_t p = first; p < n; p += 4) {
      const float* wi = xi + p;  // window of 33 samples ending at sample p of the chunk
      const float* wq = xq + p;
      float si0 = a_taps_[16] * wi[16], sq0 = a_taps_[16] * wq[16], si1 = 0, sq1 = 0, si2 = 0, sq2 = 0, si3 = 0, sq3 = 0;
      for (size_t k = 0; k < 16; k += 4) {
        si0 += a_taps_[k] * (wi[k] + wi[32 - k]);
        sq0 += a_taps_[k] * (wq[k] + wq[32 - k]);
        si1 += a_taps_[k + 1] * (wi[k + 1] + wi[31 - k]);
        sq1 += a_taps_[k + 1] * (wq[k + 1] + wq[31 - k]);
        si2 += a_taps_[k + 2] * (wi[k + 2] + wi[30 - k]);
        sq2 += a_taps_[k + 2] * (wq[k + 2] + wq[30 - k]);
        si3 += a_taps_[k + 3] * (wi[k + 3] + wi[29 - k]);
        sq3 += a_taps_[k + 3] * (wq[k + 3] + wq[29 - k]);
      }
      const float ai = (si0 + si1) + (si2 + si3), aq = (sq0 + sq1) + (sq2 + sq3);
      written += stage_b(ai, aq, out + written, written < capacity ? capacity - written : 0);
    }
    a_phase_ = static_cast<unsigned>((a_phase_ + n) % 4);
    std::copy(xi + n, xi + n + kH, a_hist_i_);
    std::copy(xq + n, xq + n + kH, a_hist_q_);
  }
  return written;
}

size_t Demod::stage_b(float ai, float aq, float* out, size_t room) {
  constexpr float kWfmGain = 240000.0f / (2 * kPi * 75000.0f);   // +/-75 kHz deviation -> +/-1
  constexpr float kNfmGain = 48000.0f / (2 * kPi * 5000.0f);     // +/-5 kHz deviation -> +/-1
  static const float deemph_alpha = 1.0f - std::exp(-1.0f / (48000.0f * 75e-6f));  // computed once
  if (mode_ == Mode::wfm) {
    // Discriminator at 240 kS/s: phase advance between consecutive samples.
    const float re = ai * prev_i_ + aq * prev_q_;
    const float im = aq * prev_i_ - ai * prev_q_;
    prev_i_ = ai;
    prev_q_ = aq;
    float audio;
    if (!stage_b_wfm_.push(fast_atan2(im, re) * kWfmGain, &audio)) return 0;
    deemph_ += deemph_alpha * (audio - deemph_);
    if (!room) return 0;
    out[0] = std::clamp(deemph_, -1.0f, 1.0f);
    return 1;
  }
  float bi, bq;
  if (!stage_b_nb_.push(ai, aq, &bi, &bq)) return 0;
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
  if (!room) return 0;
  out[0] = std::clamp(audio, -1.0f, 1.0f);
  return 1;
}

}  // namespace orcsdr::dsp

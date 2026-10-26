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
  n_taps = std::min<size_t>(t.size(), 65);
  std::copy(t.begin(), t.begin() + n_taps, taps);
  decim = d;
  reset();
}
void Demod::ComplexFir::reset() {
  std::fill(hist_i, hist_i + 130, 0.0f);
  std::fill(hist_q, hist_q + 130, 0.0f);
  pos = 0;
  phase = 0;
}
bool Demod::ComplexFir::push(float i, float q, float* oi, float* oq) {
  const size_t n = n_taps;
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
  n_taps = std::min<size_t>(t.size(), 65);
  std::copy(t.begin(), t.begin() + n_taps, taps);
  decim = d;
  reset();
}
void Demod::RealFir::reset() {
  std::fill(hist, hist + 130, 0.0f);
  pos = 0;
  phase = 0;
}
bool Demod::RealFir::push(float x, float* out) {
  const size_t n = n_taps;
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
  set_input_rate(kInputRate);
  stage_b_nb_.init(lowpass(65, 7500.0f / 240000.0f), 5);
  stage_b_wfm_.init(lowpass(65, 15000.0f / 240000.0f), 5);
}

void Demod::set_input_rate(uint32_t rate) {
  rate = std::max<uint32_t>(240000, std::min<uint32_t>(rate, 2400000));
  uint32_t stage_rate;
  if (rate > 960000) {
    pre_ = rate / 480000;  // 2.4 MS/s -> 5
    rate_ = pre_ * 480000;
    stage_rate = 480000;
  } else {
    pre_ = 1;
    rate_ = rate / 240000 * 240000;
    stage_rate = rate_;
  }
  a_decim_ = stage_rate / 240000;
  a_ntaps_ = 33;
  const std::vector<float> a = lowpass(a_ntaps_, 100000.0f / static_cast<float>(stage_rate));
  std::fill(a_taps_, a_taps_ + kMaxTaps, 0.0f);
  std::copy(a.begin(), a.end(), a_taps_);
  reset();
}

void Demod::build_ssb(Mode mode) {
  constexpr size_t n = SsbFilter::kTaps;
  const float fs = 48000.0f, f_lo = 300.0f, f_hi = 2700.0f;
  const std::vector<float> lp = lowpass(n, (f_hi - f_lo) / 2.0f / fs);
  const float center = (mode == Mode::usb ? 1.0f : -1.0f) * (f_lo + f_hi) / 2.0f;
  const float mid = (n - 1) / 2.0f;
  for (size_t k = 0; k < n; ++k) {
    const float ph = 2 * kPi * center / fs * (static_cast<float>(k) - mid);
    ssb_.re[k] = lp[k] * std::cos(ph);
    ssb_.im[k] = lp[k] * std::sin(ph);
  }
  std::fill(ssb_.hist_i, ssb_.hist_i + 2 * n, 0.0f);
  std::fill(ssb_.hist_q, ssb_.hist_q + 2 * n, 0.0f);
  ssb_.pos = 0;
  ssb_built_ = mode;
}

void Demod::set_mode(Mode mode) {
  if (mode == mode_) return;
  mode_ = mode;
  reset();
}

void Demod::reset() {
  std::fill(a_hist_i_, a_hist_i_ + kMaxTaps - 1, 0.0f);
  std::fill(a_hist_q_, a_hist_q_ + kMaxTaps - 1, 0.0f);
  a_phase_ = 0;
  std::fill(cic_i1_, cic_i1_ + 2, 0u);
  std::fill(cic_i2_, cic_i2_ + 2, 0u);
  std::fill(cic_d1_, cic_d1_ + 2, 0u);
  std::fill(cic_d2_, cic_d2_ + 2, 0u);
  cic_count_ = 0;
  stage_b_nb_.reset();
  stage_b_wfm_.reset();
  prev_i_ = prev_q_ = deemph_ = dc_ = nfm_lp_ = 0;
  level_ = 0.1f;
  chan_pow_ = 1e-12f;
}

// Second-order CIC decimator by pre_ on the raw 8-bit samples. The integrators wrap modulo 2^32, which is exact for the
// comb differences that follow.
size_t Demod::cic_decimate(const uint8_t* src, size_t n, float* oi, float* oq) {
  const unsigned R = pre_;
  const float norm = 1.0f / static_cast<float>(R * R);
  uint32_t a1 = cic_i1_[0], a2 = cic_i2_[0], b1 = cic_i1_[1], b2 = cic_i2_[1];
  uint32_t d1a = cic_d1_[0], d2a = cic_d2_[0], d1b = cic_d1_[1], d2b = cic_d2_[1];
  unsigned count = cic_count_;
  size_t out = 0;
  for (size_t k = 0; k < n; ++k) {
    a1 += src[2 * k];
    a2 += a1;
    b1 += src[2 * k + 1];
    b2 += b1;
    if (++count == R) {
      count = 0;
      const uint32_t ci = a2 - d1a;
      d1a = a2;
      const uint32_t ei = ci - d2a;
      d2a = ci;
      const uint32_t cq = b2 - d1b;
      d1b = b2;
      const uint32_t eq = cq - d2b;
      d2b = cq;
      oi[out] = (static_cast<float>(static_cast<int32_t>(ei)) * norm - 127.5f) * (1.0f / 127.5f);
      oq[out] = (static_cast<float>(static_cast<int32_t>(eq)) * norm - 127.5f) * (1.0f / 127.5f);
      ++out;
    }
  }
  cic_i1_[0] = a1;
  cic_i2_[0] = a2;
  cic_i1_[1] = b1;
  cic_i2_[1] = b2;
  cic_d1_[0] = d1a;
  cic_d2_[0] = d2a;
  cic_d1_[1] = d1b;
  cic_d2_[1] = d2b;
  cic_count_ = count;
  return out;
}

size_t Demod::process(const uint8_t* iq, size_t bytes, float* out, size_t capacity) {
  size_t written = 0;
  const size_t kH = a_ntaps_ - 1;
  const size_t half = kH / 2;
  const unsigned decim = a_decim_;
  size_t pairs_left = bytes / 2;
  const uint8_t* src = iq;
  while (pairs_left) {
    const size_t n = std::min(pairs_left, kChunk);
    pairs_left -= n;
    float* xi = xi_;
    float* xq = xq_;
    std::copy(a_hist_i_, a_hist_i_ + kH, xi);
    std::copy(a_hist_q_, a_hist_q_ + kH, xq);
    size_t m;  // samples entering the filter stage
    if (pre_ > 1) {
      m = cic_decimate(src, n, xi + kH, xq + kH);
    } else {
      static const struct Lut {
        float v[256];
        Lut() {
          for (int i = 0; i < 256; ++i) v[i] = (static_cast<float>(i) - 127.5f) * (1.0f / 127.5f);
        }
      } lut;
      for (size_t k = 0; k < n; ++k) {
        xi[kH + k] = lut.v[src[2 * k]];
        xq[kH + k] = lut.v[src[2 * k + 1]];
      }
      m = n;
    }
    src += 2 * n;
    const size_t first = decim - 1 - a_phase_;  // index of the first output in this chunk
    for (size_t p = first; p < m; p += decim) {
      const float* wi = xi + p;  // window of a_ntaps_ samples ending at sample p of the chunk
      const float* wq = xq + p;
      float si0 = a_taps_[half] * wi[half], sq0 = a_taps_[half] * wq[half], si1 = 0, sq1 = 0, si2 = 0, sq2 = 0, si3 = 0, sq3 = 0;
      for (size_t k = 0; k < half; k += 4) {  // half is a multiple of 4 (16 or 32); taps are symmetric
        si0 += a_taps_[k] * (wi[k] + wi[kH - k]);
        sq0 += a_taps_[k] * (wq[k] + wq[kH - k]);
        si1 += a_taps_[k + 1] * (wi[k + 1] + wi[kH - 1 - k]);
        sq1 += a_taps_[k + 1] * (wq[k + 1] + wq[kH - 1 - k]);
        si2 += a_taps_[k + 2] * (wi[k + 2] + wi[kH - 2 - k]);
        sq2 += a_taps_[k + 2] * (wq[k + 2] + wq[kH - 2 - k]);
        si3 += a_taps_[k + 3] * (wi[k + 3] + wi[kH - 3 - k]);
        sq3 += a_taps_[k + 3] * (wq[k + 3] + wq[kH - 3 - k]);
      }
      const float ai = (si0 + si1) + (si2 + si3), aq = (sq0 + sq1) + (sq2 + sq3);
      written += stage_b(ai, aq, out + written, written < capacity ? capacity - written : 0);
    }
    a_phase_ = static_cast<unsigned>((a_phase_ + m) % decim);
    std::copy(xi + m, xi + m + kH, a_hist_i_);
    std::copy(xq + m, xq + m + kH, a_hist_q_);
  }
  return written;
}

float Demod::channel_dbfs() const { return 10.0f * std::log10(std::max(chan_pow_, 1e-12f)); }

size_t Demod::stage_b(float ai, float aq, float* out, size_t room) {
  constexpr float kWfmGain = 240000.0f / (2 * kPi * 75000.0f);   // +/-75 kHz deviation -> +/-1
  constexpr float kNfmGain = 48000.0f / (2 * kPi * 5000.0f);     // +/-5 kHz deviation -> +/-1
  static const float deemph_alpha = 1.0f - std::exp(-1.0f / (48000.0f * 75e-6f));  // computed once
  if (mode_ == Mode::wfm) {
    chan_pow_ += 0.001f * (ai * ai + aq * aq - chan_pow_);
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
  chan_pow_ += 0.01f * (bi * bi + bq * bq - chan_pow_);  // at 48 kS/s
  float audio;
  if (mode_ == Mode::usb || mode_ == Mode::lsb) {
    if (ssb_built_ != mode_) build_ssb(mode_);
    constexpr size_t n = SsbFilter::kTaps;
    ssb_.hist_i[ssb_.pos] = ssb_.hist_i[ssb_.pos + n] = bi;
    ssb_.hist_q[ssb_.pos] = ssb_.hist_q[ssb_.pos + n] = bq;
    ssb_.pos = ssb_.pos + 1 == n ? 0 : ssb_.pos + 1;
    const float* hi = &ssb_.hist_i[ssb_.pos];
    const float* hq = &ssb_.hist_q[ssb_.pos];
    float yr = 0, yi = 0;
    for (size_t k = 0; k < n; ++k) {  // complex multiply-accumulate; newest sample is hist[n-1]
      const float xi = hi[n - 1 - k], xq = hq[n - 1 - k];
      yr += ssb_.re[k] * xi - ssb_.im[k] * xq;
      yi += ssb_.re[k] * xq + ssb_.im[k] * xi;
    }
    const float env = std::sqrt(yr * yr + yi * yi);
    level_ += 0.0005f * (env - level_);  // slow AGC on the speech envelope
    audio = yr / std::max(level_, 0.01f) * 0.35f;
  } else if (mode_ == Mode::nfm) {
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

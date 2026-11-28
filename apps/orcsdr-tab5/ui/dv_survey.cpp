#include "dv_survey.hpp"

#include <algorithm>
#include <cmath>

namespace orcsdr::dv_survey {
namespace {
constexpr float kPi = 3.14159265358979f;
std::atomic<Survey*> g_tap{nullptr};

// In-place radix-2 FFT of kFft points (decimation in time) with the precomputed twiddles.
void fft(float* re, float* im, const float* twr, const float* twi) noexcept {
  for (size_t i = 1, j = 0; i < kFft; ++i) {                    // bit reversal
    size_t bit = kFft >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
  }
  for (size_t len = 2; len <= kFft; len <<= 1) {
    const size_t half = len >> 1, stride = kFft / len;
    for (size_t i = 0; i < kFft; i += len) {
      for (size_t k = 0; k < half; ++k) {
        const float wr = twr[k * stride], wi = twi[k * stride];
        const size_t a = i + k, b = a + half;
        const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - xr; im[b] = im[a] - xi;
        re[a] += xr; im[a] += xi;
      }
    }
  }
}
}  // namespace

void init(Work& w) noexcept {
  for (size_t i = 0; i < kFft; ++i) w.win[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(kFft - 1));
  for (size_t i = 0; i < kFft / 2; ++i) {
    const float a = -2.0f * kPi * static_cast<float>(i) / static_cast<float>(kFft);
    w.twr[i] = std::cos(a); w.twi[i] = std::sin(a);
  }
}

void analyze(Work& w, size_t frames) noexcept {
  const size_t nf = (frames == 0 || frames > kMaxFrames) ? kFrames : frames;
  std::fill_n(w.pw, kFft, 0.0f);
  std::fill_n(w.pw_peak, kFft, 0.0f);
  for (size_t f = 0; f < nf; ++f) {
    const uint8_t* p = w.cap + f * kFft * 2;
    for (size_t i = 0; i < kFft; ++i) {
      w.re[i] = (static_cast<float>(p[2 * i]) - 127.5f) * (1.0f / 127.5f) * w.win[i];
      w.im[i] = (static_cast<float>(p[2 * i + 1]) - 127.5f) * (1.0f / 127.5f) * w.win[i];
    }
    fft(w.re, w.im, w.twr, w.twi);
    for (size_t k = 0; k < kFft; ++k) {
      const float mag2 = w.re[k] * w.re[k] + w.im[k] * w.im[k];
      const size_t dst = (k + kFft / 2) % kFft;
      w.pw[dst] += mag2;
      if (mag2 > w.pw_peak[dst]) w.pw_peak[dst] = mag2;
    }
  }
  const float inv = 1.0f / static_cast<float>(nf);
  for (size_t k = 0; k < kFft; ++k) w.pw[k] *= inv;
}

float channel_db(const Work& w, uint32_t rate, int32_t offset_hz, bool peak_blend) noexcept {
  const double bw = static_cast<double>(rate) / kFft;
  const double mid = kFft / 2.0;
  long lo = static_cast<long>(std::ceil((offset_hz - kChannelHz / 2.0) / bw + mid));
  long hi = static_cast<long>(std::floor((offset_hz + kChannelHz / 2.0) / bw + mid));
  if (hi < lo) lo = hi = static_cast<long>(std::lround(offset_hz / bw + mid));
  if (lo < 1 || hi > static_cast<long>(kFft) - 2) return -200.0f;
  double sum = 0, psum = 0;
  for (long k = lo; k <= hi; ++k) { sum += w.pw[k]; psum += w.pw_peak[k]; }
  const double cnt = static_cast<double>(hi - lo + 1);
  const float avg_db = 10.0f * std::log10(static_cast<float>(sum / cnt) + 1e-12f);
  if (!peak_blend) return avg_db;
  const float pk_db = 10.0f * std::log10(static_cast<float>(psum / cnt) + 1e-12f) - 4.0f;
  return pk_db > avg_db ? pk_db : avg_db;
}

bool Survey::start(const dv::Segment* segs, size_t n, uint32_t now_ms, uint8_t first_rate_index) noexcept {
  if (!work_ || !segs || n == 0 || n > dv::kMaxSegments || first_rate_index >= kRates) return false;
  for (size_t i = 0; i < n; ++i) {
    if (segs[i].count == 0 || segs[i].step_hz == 0) return false;
    segs_[i] = segs[i];
  }
  nseg_ = n; seg_ = 0; seg_started_ = false;
  rate_i_ = first_rate_index; retries_ = 0; rate_loss_run_ = 0;
  last_tuned_rate_ = 0; rate_changed_ = true;
  windows_done_ = 0; lossy_ = 0; rate_steps_ = 0; checked_ = 0; skirts_ = 0; skipped_ = 0; hot_n_ = 0; last_floor_ = 0;
  frames_.store(0, std::memory_order_relaxed);
  state_since_ms_ = now_ms;
  plan_window();
  return state() != State::done;
}

void Survey::stop() noexcept { state_.store(State::idle, std::memory_order_release); tune_pending_ = false; }

uint32_t Survey::windows_total() const noexcept {
  const double s = 0.375 * rate();
  uint32_t total = 0;
  for (size_t i = 0; i < nseg_; ++i) {
    const double span = static_cast<double>(segs_[i].count - 1) * segs_[i].step_hz;
    total += static_cast<uint32_t>(std::ceil((span + s / 2.0) / s)) + 1u;
  }
  return total;
}

void Survey::plan_window() noexcept {
  const int64_t s = static_cast<int64_t>(0.375 * rate());
  while (seg_ < nseg_) {
    const int64_t f0 = segs_[seg_].start_hz;
    if (!seg_started_) { centre_ = f0 - s / 2; seg_started_ = true; }
    if (centre_ < static_cast<int64_t>(dv::kMinChannelHz)) centre_ = dv::kMinChannelHz;
    if (centre_ > static_cast<int64_t>(dv::kMaxChannelHz)) { ++seg_; seg_started_ = false; continue; }
    center_hz_ = static_cast<uint32_t>(centre_);
    tune_pending_ = true;
    state_.store(State::tuning, std::memory_order_release);
    return;
  }
  trim_skirts();
  state_.store(State::done, std::memory_order_release);
}

void Survey::trim_skirts() noexcept {
  bool drop[kMaxHot] = {};
  for (size_t i = 0; i < hot_n_; ++i)
    for (size_t j = 0; j < hot_n_ && !drop[i]; ++j) {
      const uint32_t d = hot_[j].hz > hot_[i].hz ? hot_[j].hz - hot_[i].hz : hot_[i].hz - hot_[j].hz;
      drop[i] = j != i && d <= kSkirtHz && hot_[j].margin_db >= hot_[i].margin_db + kSkirtDb;
    }
  size_t w = 0;
  for (size_t i = 0; i < hot_n_; ++i) {
    if (drop[i]) { ++skirts_; continue; }
    hot_[w++] = hot_[i];                               // order (strongest first) is kept
  }
  hot_n_ = w;
}

void Survey::add_hot(uint32_t hz, float margin, uint8_t seg) noexcept {
  for (size_t i = 0; i < hot_n_; ++i) {
    if (hot_[i].hz != hz) continue;
    if (margin <= hot_[i].margin_db) return;
    hot_[i].margin_db = margin;
    while (i > 0 && hot_[i - 1].margin_db < hot_[i].margin_db) { std::swap(hot_[i - 1], hot_[i]); --i; }
    return;
  }
  size_t at = hot_n_;
  if (hot_n_ < kMaxHot) {
    ++hot_n_;
  } else if (nseg_ > 1u) {
    // Per-band fair-share quota: every selected band gets at least fair_share slots even if another band
    // has 100+ loud analog/800 MHz carriers. Quiet bands automatically yield unused slots to busier bands.
    const size_t fair_share = std::max<size_t>(16u, kMaxHot / nseg_);
    size_t seg_cnt[dv::kMaxSegments]{};
    for (size_t i = 0; i < kMaxHot; ++i) {
      if (hot_[i].seg < dv::kMaxSegments) ++seg_cnt[hot_[i].seg];
    }
    const bool under_quota = seg < dv::kMaxSegments && seg_cnt[seg] < fair_share;
    size_t evict = kMaxHot;
    if (under_quota) {
      for (size_t i = kMaxHot; i > 0; --i) {
        const size_t k = i - 1;
        if (hot_[k].seg < dv::kMaxSegments && seg_cnt[hot_[k].seg] > fair_share) { evict = k; break; }
      }
    }
    if (evict == kMaxHot) {
      for (size_t i = kMaxHot; i > 0; --i) {
        const size_t k = i - 1;
        if (hot_[k].margin_db >= margin) break;
        if (hot_[k].seg == seg || (hot_[k].seg < dv::kMaxSegments && seg_cnt[hot_[k].seg] > fair_share)) {
          evict = k;
          break;
        }
      }
    }
    if (evict == kMaxHot) return;
    at = evict;
  } else {
    if (hot_[kMaxHot - 1].margin_db >= margin) return;
    at = kMaxHot - 1;
  }
  hot_[at] = {hz, margin, seg};
  while (at > 0 && hot_[at - 1].margin_db < hot_[at].margin_db) { std::swap(hot_[at - 1], hot_[at]); --at; }
  while (at + 1 < hot_n_ && hot_[at + 1].margin_db > hot_[at].margin_db) { std::swap(hot_[at + 1], hot_[at]); ++at; }
}

void Survey::evaluate() noexcept {
  Work& w = *work_;
  const uint8_t nf = (cfg_.frames == 0 || cfg_.frames > kMaxFrames) ? static_cast<uint8_t>(kFrames) : cfg_.frames;
  analyze(w, nf);
  const uint32_t rate_hz = rate();
  const dv::Segment& sg = segs_[seg_];
  const int64_t half = static_cast<int64_t>(0.375 * rate_hz);
  const int64_t c = center_hz_;
  const int64_t f0 = sg.start_hz;
  int64_t lo = (c - half - f0 + static_cast<int64_t>(sg.step_hz) - 1) / static_cast<int64_t>(sg.step_hz);
  if (c - half - f0 < 0) lo = 0;
  int64_t hi = (c + half - f0) / static_cast<int64_t>(sg.step_hz);
  if (c + half - f0 < 0) hi = -1;
  hi = std::min<int64_t>(hi, static_cast<int64_t>(sg.count) - 1);
  size_t n = 0;
  for (int64_t i = lo; i <= hi && n < kMaxWindowChannels; ++i) {
    const uint32_t f = static_cast<uint32_t>(f0 + i * sg.step_hz);
    const int32_t off = static_cast<int32_t>(static_cast<int64_t>(f) - c);
    if (std::abs(off) < static_cast<int32_t>(kDcGuardHz)) continue;
    w.lv[n] = channel_db(w, rate_hz, off, cfg_.peak_hold);
    w.lf[n] = f;
    ++n;
  }
  if (n < 8) return;                                  // too few channels to judge a floor
  float* tmp = w.re;                                  // scratch: the spectrum is already in pw
  std::copy_n(w.lv, n, tmp);
  std::sort(tmp, tmp + n);
  const float floor_db = tmp[n / 4];                  // 25th percentile: robust while most of the window is empty
  last_floor_ = floor_db;
  checked_ += static_cast<uint32_t>(n);
  for (size_t i = 0; i < n; ++i) {
    if (w.lv[i] > -150.0f && w.lv[i] >= floor_db + cfg_.margin_db) {
      if (skip_fn_ && skip_fn_(w.lf[i], skip_ctx_)) { ++skipped_; continue; }
      add_hot(w.lf[i], w.lv[i] - floor_db, static_cast<uint8_t>(seg_));
    }
  }
}

void Survey::window_failed(uint32_t now_ms, bool lossy) noexcept {
  if (lossy) ++lossy_;
  ++retries_;
  const uint8_t lim = (lossy && windows_done_ == 0u && cfg_.max_retries > 1u) ? 1u : cfg_.max_retries;
  if (retries_ > lim) {
    if (rate_i_ + 1u < kRates) { ++rate_i_; ++rate_steps_; retries_ = 0; }
    else { state_.store(State::failed, std::memory_order_release); return; }
  }
  tune_pending_ = true;                               // same window again (at the new rate when it stepped down)
  state_since_ms_ = now_ms;
  state_.store(State::tuning, std::memory_order_release);
}

bool Survey::service(uint32_t now_ms, uint64_t lost_total, uint32_t* tune_hz, uint32_t* tune_rate) noexcept {
  const uint8_t target_frames = (cfg_.frames == 0 || cfg_.frames > kMaxFrames) ? static_cast<uint8_t>(kFrames) : cfg_.frames;
  switch (state()) {
    case State::tuning:
      if (!tune_pending_) return false;
      tune_pending_ = false;
      *tune_hz = center_hz_; *tune_rate = rate();
      rate_changed_ = (last_tuned_rate_ != *tune_rate);
      last_tuned_rate_ = *tune_rate;
      return true;
    case State::settling: {
      const uint32_t need_settle = rate_changed_ ? cfg_.settle_rate_change_ms : cfg_.settle_ms;
      if (now_ms - state_since_ms_ >= need_settle) {
        frames_.store(0, std::memory_order_relaxed);
        lost_at_arm_ = lost_total;
        state_since_ms_ = now_ms;
        state_.store(State::capturing, std::memory_order_release);
      }
      return false;
    }
    case State::capturing:
      if (frames_.load(std::memory_order_acquire) >= target_frames) {
        if (lost_total != lost_at_arm_) { window_failed(now_ms, true); return false; }
        retries_ = 0;
        evaluate();
        ++windows_done_;
        const int64_t s = static_cast<int64_t>(0.375 * rate());
        if (centre_ >= static_cast<int64_t>(segs_[seg_].start_hz) + static_cast<int64_t>(segs_[seg_].count - 1) * segs_[seg_].step_hz) {
          ++seg_; seg_started_ = false;
        } else {
          centre_ += s;
        }
        plan_window();
      } else if (now_ms - state_since_ms_ > (rate_changed_ ? cfg_.capture_ms_max + 2000u : cfg_.capture_ms_max)) {
        window_failed(now_ms, false);
      }
      return false;
    default:
      return false;
  }
}

void Survey::tune_result(bool accepted, uint32_t now_ms) noexcept {
  if (state() != State::tuning) return;
  if (!accepted) { window_failed(now_ms, false); return; }
  state_since_ms_ = now_ms;
  state_.store(State::settling, std::memory_order_release);
}

void Survey::offer(const uint8_t* cu8, size_t bytes, uint32_t sample_rate) noexcept {
  if (state() != State::capturing || !work_ || !cu8 || bytes < kFft * 2) return;
  const uint32_t want = rate();
  if (sample_rate + want / 100u < want || sample_rate > want + want / 100u) return;     // stale block from another rate
  const uint8_t target_frames = (cfg_.frames == 0 || cfg_.frames > kMaxFrames) ? static_cast<uint8_t>(kFrames) : cfg_.frames;
  const uint8_t n = frames_.load(std::memory_order_relaxed);
  if (n >= target_frames) return;
  std::copy_n(cu8, kFft * 2, work_->cap + static_cast<size_t>(n) * kFft * 2);
  frames_.store(static_cast<uint8_t>(n + 1), std::memory_order_release);
}

void install(Survey* s) noexcept { g_tap.store(s, std::memory_order_release); }
bool capturing() noexcept { Survey* s = g_tap.load(std::memory_order_acquire); return s && s->capturing(); }
bool active() noexcept {
  Survey* s = g_tap.load(std::memory_order_acquire);
  if (!s) return false;
  const State st = s->state();
  return st == State::tuning || st == State::settling || st == State::capturing;
}
void offer_iq(const uint8_t* cu8, size_t bytes, uint32_t sample_rate) noexcept {
  Survey* s = g_tap.load(std::memory_order_acquire);
  if (s) s->offer(cu8, bytes, sample_rate);
}

}  // namespace orcsdr::dv_survey

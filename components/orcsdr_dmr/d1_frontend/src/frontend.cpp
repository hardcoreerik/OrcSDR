// Independently developed 48 kS/s 4FSK front-end. D0 ETSI sync is read-only.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "frontend.hpp"
#include "orcsdr_dmr/d0.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace orcsdr::dmr::d1 {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kToHz = static_cast<float>(kSampleRate / (2.0 * kPi));
constexpr std::uint64_t kSyncSymbols = 24;
constexpr float kMinimumSyncCorrelation = 0.89F;
// Only reject effectively-zero complex IQ, not a weak but numerically
// representable radio capture. The atan2 discriminator divides out the
// absolute amplitude; a 1e-5 *power* threshold previously discarded every
// sample of a confirmed real-air voice-terminator recording (peak IQ <0.002).
// This is a numerical-floor guard, NOT an RF squelch or signal-quality test.
constexpr float kMinPower = 1.0e-10F;
constexpr double kRolloff = 0.2;

// The eight ETSI syncs are four balanced complementary pairs, all outer
// levels (+/-3). The compile-time checks keep this screen tied to D0's words.
constexpr bool balanced_complements() noexcept {
    for (std::size_t k = 0; k < kSyncCount; k += 2) {
        int sum = 0;
        for (std::size_t i = 0; i < kSyncSymbols; ++i) {
            const auto a = dibit_level(static_cast<std::uint8_t>((kSyncWords[k] >> (46-2*i)) & 3U));
            const auto b = dibit_level(static_cast<std::uint8_t>((kSyncWords[k+1] >> (46-2*i)) & 3U));
            if ((a != 3 && a != -3) || b != -a) return false;
            sum += a;
        }
        if (sum != 0) return false;
    }
    return true;
}
static_assert(kSyncCount == 8 && kSyncSymbols == 24 && balanced_complements(), "DMR sync screen invariants");
constexpr auto make_sync_levels() noexcept {
    std::array<std::array<float, kSyncSymbols>, kSyncCount/2> levels{};
    for (std::size_t k = 0; k < levels.size(); ++k)
        for (std::size_t i = 0; i < kSyncSymbols; ++i)
            levels[k][i] = static_cast<float>(dibit_level(static_cast<std::uint8_t>((kSyncWords[2*k] >> (46-2*i)) & 3U)));
    return levels;
}
constexpr auto kScreenLevels = make_sync_levels();

// A 6-symbol, beta=0.2 root raised cosine for the *demodulated frequency*.
// Full spectrum Nyquist receiver pulse is completed by matching a shaped TX.
// No Tx filtering is assumed for validity of the host-only front-end API.
double rrc(double x) noexcept {
    const auto a = kRolloff;
    if (std::abs(x) < 1e-10) {
        return 1.0 + a * (4.0 / kPi - 1.0);
    }
    if (std::abs(std::abs(x) - 1.0 / (4.0 * a)) < 1e-9) {
        return (a / std::sqrt(2.0)) *
               ((1.0 + 2.0 / kPi) * std::sin(kPi / (4.0 * a)) +
                (1.0 - 2.0 / kPi) * std::cos(kPi / (4.0 * a)));
    }
    return (std::sin(kPi * (1.0 - a) * x) +
            4.0 * a * x * std::cos(kPi * (1.0 + a) * x)) /
           (kPi * x * (1.0 - 16.0 * a * a * x * x));
}
} // namespace

Frontend::Frontend() noexcept {
    double sum = 0.0;
    for (std::size_t i = 0; i < kPulseTaps; ++i) {
        taps_[i] = static_cast<float>(rrc((static_cast<double>(i) - 30.0) / 10.0));
        sum += taps_[i];
    }
    for (auto& t : taps_) {
        t = static_cast<float>(static_cast<double>(t) / sum);
    }
    reset();
}

void Frontend::reset_tracking() noexcept {
    discr_history_.fill(0.0F);
    filtered_history_.fill(0.0F);
    discr_head_ = 0;
    processed_ = 0;
    previous_ = {};
    have_previous_ = false;
    locked_ = false;
    next_center_int_ = 0;
    next_center_frac_ = 0.0F;
    timing_step_ = 10.0F;
    cfo_hz_ = 0.0F;
    level_hz_ = kInnerDeviationHz;
    prev_level_ = 0.0F;
    have_prev_level_ = false;
    decision_mse_ = 0.0F;
    bad_symbols_ = 0;
    iq_power_ = 0.0F;
    acq_pending_ = false;
    acq_best_pos_ = 0;
    acq_best_corr_ = 0.0F;
    acq_best_cfo_ = 0.0F;
    acq_best_level_ = 0.0F;
    acq_prev_corr_ = -1.0F;
    acq_next_corr_ = -1.0F;
    acq_best_kind_ = 255U;
    acq_countdown_ = 0;
    acq_age_ = 0;
    preroll_first_center_ = 0;
    preroll_frac_ = 0.0F;
    preroll_cursor_ = static_cast<std::uint8_t>(kPrerollSymbols);
    stats_.locked = false;
    stats_.sync_kind = 255U;
    stats_.sync_correlation = 0.0F;
    stats_.carrier_offset_hz = 0.0F;
    stats_.deviation_per_level_hz = 0.0F;
    stats_.decision_rms_hz = 0.0F;
    stats_.rms_iq = 0.0F;
    stats_.timing_step_samples = 10.0F;
}
void Frontend::reset() noexcept {
    stats_ = {};
    reset_tracking();
}
void Frontend::discontinuity() noexcept {
    ++stats_.discontinuities;
    if (locked_) ++stats_.lost_locks;
    reset_tracking();
}

float Frontend::filtered_at(std::uint64_t i) const noexcept {
    return filtered_history_[static_cast<std::size_t>(i) % kHistory];
}
float Frontend::interpolate(std::uint64_t base, float frac) const noexcept {
    std::int64_t idx = static_cast<std::int64_t>(base);
    while (frac < 0.0F) { --idx; frac += 1.0F; }
    while (frac >= 1.0F) { ++idx; frac -= 1.0F; }
    const auto n = static_cast<std::uint64_t>(idx);
    const float a = filtered_at(n);
    if (frac <= 0.0F) return a;
    return a + frac * (filtered_at(n + 1) - a);
}

void Frontend::consume_one(const ComplexSample& sample) noexcept {
    const float power = sample.re*sample.re + sample.im*sample.im;
    const bool finite = std::isfinite(sample.re) && std::isfinite(sample.im) &&
                        std::isfinite(power) && power >= kMinPower;
    if (!finite) {
        ++stats_.invalid_iq;
        // Explicit invalid input is a discontinuity: phase cannot bridge it.
        if (locked_) ++stats_.lost_locks;
        reset_tracking();
        ++stats_.samples;
        return;
    }
    iq_power_ += 0.005F * (power - iq_power_);
    stats_.rms_iq = std::sqrt(std::max(0.0F, iq_power_));
    if (!have_previous_) {
        previous_ = sample;
        have_previous_ = true;
        ++processed_;
        ++stats_.samples;
        return;
    }
    // arg(conj(previous) * current) / dt; amplitude cancels.
    const float cross = previous_.re * sample.im - previous_.im * sample.re;
    const float dot = previous_.re * sample.re + previous_.im * sample.im;
    const float hz = std::atan2(cross, dot) * kToHz;
    previous_ = sample;
    discr_history_[discr_head_] = hz;
    float filt = 0.0F;
    // Circular convolution; 61 constant taps, no buffers or allocation.
    // Same tap order and arithmetic as before; the index walks backwards with a wrap instead of an integer modulo per tap.
    {
        std::size_t index = discr_head_;
        for (std::size_t j = 0; j < kPulseTaps; ++j) {
            filt += taps_[j] * discr_history_[index];
            index = index == 0 ? kPulseTaps - 1 : index - 1;
        }
    }
    discr_head_ = (discr_head_+1) % kPulseTaps;
    filtered_history_[static_cast<std::size_t>(processed_) % kHistory] = filt;
    ++processed_;
    ++stats_.samples;
    if (!locked_) {
        if (acq_pending_) {
            try_acquire();
        } else if (processed_ > 320 && processed_ % 2U == 0U) {
            try_acquire();
        }
    }
}

float Frontend::evaluate_sync_at(std::uint64_t end_center, float screen_min,
                                  float& out_cfo, float& out_level,
                                  std::uint8_t& out_kind) const noexcept {
    // Sync acquisition with full affine regression (offset and deviation) in single-precision float
    // (ESP32-P4 has single-precision FPU only). Mean-subtracted y avoids cancellation at large CFO.
    constexpr float kVarX = 216.0F;
    float observed[kSyncSymbols];
    float sum_y = 0.0F;
    for (std::size_t i = 0; i < kSyncSymbols; ++i) {
        const float v = filtered_at(end_center - (kSyncSymbols - 1U - i) * 10ULL);
        observed[i] = v;
        sum_y += v;
    }
    const float mean_y = sum_y / static_cast<float>(kSyncSymbols);
    float var_y = 0.0F;
    for (std::size_t i = 0; i < kSyncSymbols; ++i) {
        const float dy = observed[i] - mean_y;
        observed[i] = dy;
        var_y += dy * dy;
    }
    if (var_y < 100000.0F) return 0.0F;
    float best = 0.0F, best_offset = 0.0F, best_level = 0.0F;
    std::uint8_t best_kind = 255U;
    const float inv_denom = 1.0F / (kVarX * var_y);
    for (std::size_t pair = 0; pair < kScreenLevels.size(); ++pair) {
        float cov = 0.0F;
        for (std::size_t i = 0; i < kSyncSymbols; ++i) cov += kScreenLevels[pair][i] * observed[i];
        const float score = (cov * cov) * inv_denom;
        if (score < screen_min) continue;
        const std::uint8_t kind = static_cast<std::uint8_t>(cov >= 0.0F ? (2U * pair) : (2U * pair + 1U));
        const float abs_cov = std::abs(cov);
        const float gain = abs_cov / kVarX;
        if (gain < 400.0F || gain > 950.0F || std::abs(mean_y) > 4000.0F) continue;
        if (score > best) {
            best = score;
            best_offset = mean_y;
            best_level = gain;
            best_kind = kind;
        }
    }
    out_cfo = best_offset;
    out_level = best_level;
    out_kind = best_kind;
    return best;
}

void Frontend::commit_acquire() noexcept {
    constexpr std::uint64_t distance = kBurstStartToSyncEndSymbols * kSamplesPerSymbol;
    static_assert(kHistory >= distance + kPrerollSymbols + kPulseTaps,
                  "History must survive one-symbol backpressure replay");
    acq_pending_ = false;
    if (acq_best_pos_ + 1ULL <= distance + kPulseTaps) {
        ++stats_.incomplete_preroll_rejections;
        return;
    }
    if (processed_ - 1ULL - acq_best_pos_ > 8ULL) return;
    float delta = 0.0F;
    if (acq_prev_corr_ >= 0.0F && acq_next_corr_ >= 0.0F) {
        const float denom = acq_prev_corr_ - 2.0F * acq_best_corr_ + acq_next_corr_;
        if (denom < -1.0e-6F) {
            delta = std::clamp(0.5F * (acq_prev_corr_ - acq_next_corr_) / denom, -0.5F, 0.5F);
        }
    }
    locked_ = true;
    cfo_hz_ = acq_best_cfo_;
    level_hz_ = acq_best_level_;
    preroll_first_center_ = acq_best_pos_ - distance;
    preroll_frac_ = delta;
    preroll_cursor_ = 0;
    std::int64_t next_i = static_cast<std::int64_t>(acq_best_pos_) + 10;
    float next_f = delta;
    while (next_f < 0.0F) { --next_i; next_f += 1.0F; }
    while (next_f >= 1.0F) { ++next_i; next_f -= 1.0F; }
    next_center_int_ = static_cast<std::uint64_t>(next_i);
    next_center_frac_ = next_f;
    timing_step_ = 10.0F;
    have_prev_level_ = true;
    prev_level_ = static_cast<float>(dibit_level(static_cast<std::uint8_t>(
        kSyncWords[acq_best_kind_] & 3U)));
    decision_mse_ = 0.0F;
    bad_symbols_ = 0;
    stats_.locked = true;
    stats_.sync_kind = acq_best_kind_;
    stats_.sync_correlation = acq_best_corr_;
    stats_.carrier_offset_hz = cfo_hz_;
    stats_.deviation_per_level_hz = level_hz_;
    ++stats_.locks;
    ++stats_.full_preroll_acquisitions;
}

void Frontend::try_acquire() noexcept {
    constexpr std::uint64_t distance = kBurstStartToSyncEndSymbols * kSamplesPerSymbol;
    const std::uint64_t pos = processed_ - 1ULL;
    float cfo = 0.0F, level = 0.0F;
    std::uint8_t kind = 255U;
    const float score = evaluate_sync_at(pos, acq_pending_ ? 0.50F : kMinimumSyncCorrelation * 0.92F,
                                         cfo, level, kind);
    if (!acq_pending_) {
        if (score < kMinimumSyncCorrelation) return;
        if (processed_ <= distance + kPulseTaps) {
            ++stats_.incomplete_preroll_rejections;
            return;
        }
        // Check pos-1 too since idle probing steps by 2 samples.
        float cfo_p = 0.0F, lvl_p = 0.0F;
        std::uint8_t kind_p = 255U;
        const float score_p = (pos > 231ULL) ? evaluate_sync_at(pos - 1ULL, 0.50F, cfo_p, lvl_p, kind_p) : 0.0F;
        if (score_p > score && kind_p == kind) {
            float cfo_pp = 0.0F, lvl_pp = 0.0F;
            std::uint8_t kind_pp = 255U;
            const float score_pp = (pos > 232ULL) ? evaluate_sync_at(pos - 2ULL, 0.50F, cfo_pp, lvl_pp, kind_pp) : -1.0F;
            acq_best_pos_ = pos - 1ULL;
            acq_best_corr_ = score_p;
            acq_best_cfo_ = cfo_p;
            acq_best_level_ = lvl_p;
            acq_best_kind_ = kind_p;
            acq_prev_corr_ = score_pp;
            acq_next_corr_ = score;
            acq_countdown_ = 1U;
        } else {
            acq_best_pos_ = pos;
            acq_best_corr_ = score;
            acq_best_cfo_ = cfo;
            acq_best_level_ = level;
            acq_best_kind_ = kind;
            acq_prev_corr_ = score_p > 0.0F ? score_p : -1.0F;
            acq_next_corr_ = -1.0F;
            acq_countdown_ = 2U;
        }
        acq_pending_ = true;
        acq_age_ = 0U;
        return;
    }
    ++acq_age_;
    if (score >= kMinimumSyncCorrelation && score > acq_best_corr_) {
        acq_prev_corr_ = (pos == acq_best_pos_ + 1ULL) ? acq_best_corr_ : -1.0F;
        acq_best_pos_ = pos;
        acq_best_corr_ = score;
        acq_best_cfo_ = cfo;
        acq_best_level_ = level;
        acq_best_kind_ = kind;
        acq_next_corr_ = -1.0F;
        acq_countdown_ = 2U;
    } else {
        if (pos == acq_best_pos_ + 1ULL) acq_next_corr_ = score;
        if (acq_countdown_ > 0U) --acq_countdown_;
    }
    if (acq_countdown_ == 0U || acq_age_ >= 4U) {
        commit_acquire();
    }
}

bool Frontend::replay_preroll(Symbol& out) noexcept {
    if (!locked_ || preroll_cursor_ >= kPrerollSymbols) return false;
    const std::uint64_t center = preroll_first_center_ +
        static_cast<std::uint64_t>(preroll_cursor_) * kSamplesPerSymbol;
    // Defensive guard against an overwritten ring, even if caller violates
    // expected producer/consumer cadence. Do not issue stale/fake decisions.
    if (center >= processed_ || processed_ - center > kHistory - 3ULL) {
        locked_ = false;
        stats_.locked = false;
        stats_.sync_kind = 255U;
        ++stats_.lost_locks;
        preroll_cursor_ = static_cast<std::uint8_t>(kPrerollSymbols);
        return false;
    }
    const float observed = interpolate(center, preroll_frac_);
    const float scaled = (observed - cfo_hz_) / level_hz_;
    const auto dibit = level_dibit(scaled);
    const float residual = observed - (cfo_hz_ +
                         level_hz_ * static_cast<float>(dibit_level(dibit)));
    out = {dibit, residual, center};
    ++preroll_cursor_;
    ++stats_.preroll_symbols;
    ++stats_.symbols;
    return true;
}

bool Frontend::decide(Symbol& out) noexcept {
    if (!locked_) return false;
    const std::uint64_t needed = next_center_int_ + (next_center_frac_ > 0.0F ? 1ULL : 0ULL);
    if (needed >= processed_) return false;
    const float observed = interpolate(next_center_int_, next_center_frac_);
    const float scaled = (observed - cfo_hz_) / level_hz_;
    const std::uint8_t dibit = level_dibit(scaled);
    const float level = static_cast<float>(dibit_level(dibit));
    const float err = observed - (cfo_hz_ + level_hz_ * level);
    out = {dibit, err, next_center_int_ + (next_center_frac_ >= 0.5F ? 1ULL : 0ULL)};
    // Gardner-like transition-centered timing-error detector (single-precision FPU).
    float step = timing_step_;
    if (have_prev_level_) {
        const float mid = interpolate(next_center_int_, next_center_frac_ - 5.0F);
        const float mid_level = (mid - cfo_hz_) / level_hz_;
        const float e = (prev_level_ - level) * (mid_level - 0.5F * (prev_level_ + level));
        const float bounded = std::clamp(e, -12.0F, 12.0F);
        timing_step_ = std::clamp(timing_step_ + 0.00004F * bounded, 9.95F, 10.05F);
        step = timing_step_ + 0.008F * bounded;
    }
    const float advance = next_center_frac_ + step;
    const auto whole = static_cast<std::int64_t>(std::floor(advance));
    next_center_int_ = static_cast<std::uint64_t>(static_cast<std::int64_t>(next_center_int_) + whole);
    next_center_frac_ = advance - static_cast<float>(whole);
    prev_level_ = level;
    have_prev_level_ = true;
    const float abs_err = std::abs(err);
    if (abs_err <= 0.65F * level_hz_) {
        cfo_hz_ = std::clamp(cfo_hz_ + 0.004F * err, -4000.0F, 4000.0F);
        if (abs_err <= 0.55F * level_hz_ && (dibit == 1U || dibit == 3U)) {
            const float signed_err = (dibit == 1U) ? err : -err;
            level_hz_ = std::clamp(level_hz_ + 0.002F * (signed_err / 3.0F), 450.0F, 900.0F);
            stats_.deviation_per_level_hz = level_hz_;
        }
    }
    if (abs_err <= 0.72F * level_hz_) {
        if (bad_symbols_ > 0) --bad_symbols_;
    } else {
        ++bad_symbols_;
    }
    decision_mse_ += 0.01F * (err * err - decision_mse_);
    stats_.decision_rms_hz = std::sqrt(std::max(0.0F, decision_mse_));
    stats_.carrier_offset_hz = cfo_hz_;
    stats_.timing_step_samples = timing_step_;
    ++stats_.symbols;
    if (bad_symbols_ >= 64U) {
        locked_ = false;
        have_prev_level_ = false;
        stats_.locked = false;
        stats_.sync_kind = 255U;
        ++stats_.lost_locks;
    }
    return true;
}

ProcessResult Frontend::process(const ComplexSample* samples, std::size_t count,
                               Symbol* output, std::size_t capacity) noexcept {
    ProcessResult result{};
    if (!samples && count!=0U) return result;
    if (!output && capacity!=0U) return result;
    if(capacity==0U) return result;
    for (std::size_t i=0;i<count;++i) {
        consume_one(samples[i]);
        ++result.consumed;
        Symbol symbol{};
        if (replay_preroll(symbol) ||
            (locked_ && decide(symbol))) {
            output[result.produced++] = symbol;
            if (result.produced == capacity) break;
        }
    }
    return result;
}
} // namespace orcsdr::dmr::d1

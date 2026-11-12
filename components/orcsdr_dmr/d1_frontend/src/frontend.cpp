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
constexpr float kMinimumSyncCorrelation = 0.955F;
constexpr float kMinPower = 0.00001F;
constexpr double kRolloff = 0.2;

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
    next_center_ = 0.0;
    timing_step_ = 10.0;
    cfo_hz_ = 0.0F;
    level_hz_ = kInnerDeviationHz;
    prev_level_ = 0.0F;
    have_prev_level_ = false;
    decision_mse_ = 0.0F;
    bad_symbols_ = 0;
    iq_power_ = 0.0F;
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
float Frontend::interpolate(double t) const noexcept {
    // t is always in [processed_-history, processed_-1].
    const double lo = std::floor(t);
    const auto n = static_cast<std::uint64_t>(lo);
    const float a = filtered_at(n);
    return a + static_cast<float>(t - lo) * (filtered_at(n+1) - a);
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
    for (std::size_t j=0; j<kPulseTaps; ++j) {
        const auto index = (discr_head_ + kPulseTaps - j) % kPulseTaps;
        filt += taps_[j] * discr_history_[index];
    }
    discr_head_ = (discr_head_+1) % kPulseTaps;
    filtered_history_[static_cast<std::size_t>(processed_) % kHistory] = filt;
    ++processed_;
    ++stats_.samples;
    if (!locked_ && processed_ > 320 && processed_ % 2U == 0U)
        try_acquire();
}

void Frontend::try_acquire() noexcept {
    // Sync acquisition with full affine regression (offset and deviation)
    // at 10-sample spacing, from the *measured* FSK discriminator waveform.
    // Only eight D0 ETSI syncs are considered. No CRC or identity is inferred.
    float observed[kSyncSymbols];
    double sum_y=0.0, sum_yy=0.0;
    for(std::size_t i=0;i<kSyncSymbols;++i) {
        observed[i]=filtered_at(processed_-1-(kSyncSymbols-1-i)*10);
        sum_y+=observed[i];
        sum_yy+=static_cast<double>(observed[i])*observed[i];
    }
    const double var_y=sum_yy-sum_y*sum_y/kSyncSymbols;
    if (var_y < 100000.0) return;
    float best=0.0F,best_offset=0.0F,best_level=0.0F;
    std::uint8_t best_kind=255U;
    for(std::size_t kind=0;kind<kSyncCount;++kind) {
        const std::uint64_t word=kSyncWords[kind];
        double sum_x=0.0,sum_xx=0.0,sum_xy=0.0;
        for(std::size_t i=0;i<kSyncSymbols;++i) {
            const auto b=static_cast<std::uint8_t>((word >> (46-2*i)) & 3U);
            const double x=dibit_level(b);
            sum_x+=x;sum_xx+=x*x;sum_xy+=x*observed[i];
        }
        const double var_x=sum_xx-sum_x*sum_x/kSyncSymbols;
        const double cov=sum_xy-sum_x*sum_y/kSyncSymbols;
        if(cov<=0.0) continue;
        const double gain=cov/var_x;
        const double offset=(sum_y-gain*sum_x)/kSyncSymbols;
        if (gain<400.0||gain>950.0||std::abs(offset)>4000.0) continue;
        const double score=(cov*cov)/(var_x*var_y);
        if(score>best) {
            best=static_cast<float>(score);
            best_offset=static_cast<float>(offset);
            best_level=static_cast<float>(gain);
            best_kind=static_cast<std::uint8_t>(kind);
        }
    }
    if(best<kMinimumSyncCorrelation) return;
    locked_=true;
    cfo_hz_=best_offset;
    level_hz_=best_level;
    // This sample is the detected end-sync center in *filtered* time.
    next_center_=static_cast<double>(processed_-1)+10.0;
    timing_step_=10.0;
    have_prev_level_=true;
    prev_level_=static_cast<float>(dibit_level(static_cast<std::uint8_t>(
        kSyncWords[best_kind]&3U)));
    decision_mse_=0.0F;
    bad_symbols_=0;
    stats_.locked=true;
    stats_.sync_kind=best_kind;
    stats_.sync_correlation=best;
    stats_.carrier_offset_hz=cfo_hz_;
    stats_.deviation_per_level_hz=level_hz_;
    ++stats_.locks;
}

bool Frontend::decide(Symbol& out) noexcept {
    if (!locked_ || next_center_>static_cast<double>(processed_-1)) return false;
    const float observed=interpolate(next_center_);
    const float scaled=(observed-cfo_hz_)/level_hz_;
    const std::uint8_t dibit=level_dibit(scaled);
    const float level=static_cast<float>(dibit_level(dibit));
    const float err=observed-(cfo_hz_+level_hz_*level);
    out={dibit,err,static_cast<std::uint64_t>(next_center_+0.5)};
    // Gardner-like transition-centered timing-error detector. With the
    // reference centered on a matched pulse, the centered transition error
    // has zero mean. Integral loop tracks small sample-clock drift.
    if (have_prev_level_) {
        const float mid=interpolate(next_center_-5.0);
        const float mid_level=(mid-cfo_hz_)/level_hz_;
        const float e=(prev_level_-level) *
                      (mid_level-0.5F*(prev_level_+level));
        const double bounded=std::clamp(static_cast<double>(e),-12.0,12.0);
        timing_step_=std::clamp(timing_step_+0.00004*bounded,9.95,10.05);
        next_center_ += timing_step_ + 0.008*bounded;
    } else {
        next_center_+=timing_step_;
    }
    prev_level_=level;
    have_prev_level_=true;
    // Extremely slow carrier tracker. A single wrong decision cannot move
    // the estimate far enough to hide an absent carrier.
    cfo_hz_=std::clamp(cfo_hz_+0.004F*err,-4000.0F,4000.0F);
    decision_mse_ += 0.01F*(err*err-decision_mse_);
    stats_.decision_rms_hz=std::sqrt(std::max(0.0F,decision_mse_));
    stats_.carrier_offset_hz=cfo_hz_;
    stats_.timing_step_samples=static_cast<float>(timing_step_);
    ++stats_.symbols;
    // Sustained severe slicer misses -> lose lock, look for next real sync.
    if (std::abs(err)>0.65F*level_hz_) ++bad_symbols_;
    else if (bad_symbols_>0) --bad_symbols_;
    if (bad_symbols_>=20) {
        locked_=false;
        have_prev_level_=false;
        stats_.locked=false;
        stats_.sync_kind=255U;
        ++stats_.lost_locks;
    }
    return true;
}

ProcessResult Frontend::process(const ComplexSample* samples, std::size_t count,
                               Symbol* output, std::size_t capacity) noexcept {
    ProcessResult result{};
    if (!samples && count!=0U) return result;
    if (!output && capacity!=0U) return result;
    // No consumption with zero output slots: prevents dropping future symbols.
    if(capacity==0U) return result;
    for (std::size_t i=0;i<count;++i) {
        consume_one(samples[i]);
        ++result.consumed;
        if (locked_ && next_center_ <= static_cast<double>(processed_-1)) {
            Symbol s{};
            if (decide(s)) {
                output[result.produced++]=s;
                if (result.produced==capacity) break;
            }
        }
    }
    return result;
}
} // namespace orcsdr::dmr::d1
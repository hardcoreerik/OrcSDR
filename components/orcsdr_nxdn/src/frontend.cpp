// Fixed-memory first-party 48-kHz IQ → NXDN48/NXDN96 hard dibits.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_nxdn/frontend.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace orcsdr::nxdn {
namespace {
constexpr double kPi = 3.14159265358979323846;
// Bounded deterministic Kaiser I0 (no special-function library on ESP32-P4).
double i0(double x) noexcept {
    double sum = 1.0, term = 1.0;
    for (unsigned n = 1; n <= 24; ++n) {
        term *= (x*x)/(4.0*n*n);
        sum += term;
        if (term < sum * 1e-13) break;
    }
    return sum;
}
double sign(double x) noexcept { return x>0 ? 1.0 : (x<0 ? -1.0 : 0.0); }
}

Frontend::Frontend(Mode mode, double input_rate_hz) noexcept : mode_(mode), fs_(input_rate_hz) {
    sps_ = static_cast<std::size_t>(std::lround(fs_/(mode == Mode::nxdn48 ? 2400.0 : 4800.0)));
    if(sps_<4U) sps_=4U;
    if(sps_>20U) sps_=20U;   // the tap/history arrays are sized for 20 samples per symbol
    channel_n_ = 2U*9U*sps_+1U;
    rrc_n_ = 2U*8U*sps_+1U;
    build_taps();
    reset();
}

void Frontend::build_taps() noexcept {
    const double cutoff=mode_ == Mode::nxdn48 ? 3500.0 : 6250.0;
    const double beta=8.6;
    const double denom=i0(beta);
    double sum=0;
    for (std::size_t k=0;k<channel_n_;++k) {
        const double m=static_cast<double>(k)-static_cast<double>(channel_n_-1U)/2;
        const double sinc=m==0 ? 2*cutoff/fs_ : std::sin(2*kPi*cutoff*m/fs_)/(kPi*m);
        const double z=2*static_cast<double>(k)/static_cast<double>(channel_n_-1U)-1;
        const double win=i0(beta*std::sqrt(std::max(0.0,1-z*z)))/denom;
        channel_taps_[k]=static_cast<float>(sinc*win);
        sum+=sinc*win;
    }
    for(std::size_t i=0;i<channel_n_;++i)
        channel_taps_[i]=static_cast<float>(channel_taps_[i]/sum);

    constexpr double a=0.20;
    double energy=0;
    for(std::size_t k=0;k<rrc_n_;++k) {
        const double t=(static_cast<double>(k)-static_cast<double>(rrc_n_-1U)/2)/static_cast<double>(sps_);
        double h;
        if(std::abs(t)<1e-9) h=1.0+a*(4.0/kPi-1.0);
        else if(std::abs(std::abs(t)-1.0/(4*a))<1e-9)
            h=a/std::sqrt(2.0)*((1.0+2.0/kPi)*std::sin(kPi/(4*a))+
                                 (1.0-2.0/kPi)*std::cos(kPi/(4*a)));
        else h=(std::sin(kPi*t*(1-a))+4*a*t*std::cos(kPi*t*(1+a)))/
               (kPi*t*(1-16*a*a*t*t));
        rrc_taps_[k]=static_cast<float>(h);
        energy+=h*h;
    }
    for(std::size_t i=0;i<rrc_n_;++i)
        rrc_taps_[i]=static_cast<float>(rrc_taps_[i]/std::sqrt(energy));
}

void Frontend::reset() noexcept {
    channel_hist_.fill({});rrc_hist_.fill(0);
    channel_pos_=0;rrc_pos_=0;
    prev_iq_={1,0};have_iq_=false;prev_rrc_=0;have_rrc_=false;
    carrier_hz_=0;nco_phase_=0;disc_sum_=0;disc_sumsq_=0;disc_count_=0;
    mm_mu_=static_cast<double>(sps_);prev_sym_=0;have_sym_=false;
    symbol_dc_=0;symbol_abs_=0;symbols_=0;
}

bool Frontend::feed(const IQSample* iq,std::size_t count,
                    DibitSink sink,void* user) noexcept {
    if(mode_!=Mode::nxdn48&&mode_!=Mode::nxdn96) return false;
    if(count&&!iq) return false;
    if(count>std::numeric_limits<std::uint64_t>::max()/2-symbols_) return false;
    for(std::size_t i=0;i<count;++i)
        if(!std::isfinite(iq[i].re)||!std::isfinite(iq[i].im)) return false;
    for(std::size_t i=0;i<count;++i) {
        // Continuous-phase IQ carrier tracking before channel filtering.
        // Single-precision trig: the double sin/cos here ran 48,000 times a second and cost more than both filters on a
        // single-precision-FPU target (ESP32-P4). The phase accumulator itself stays double; only its sine/cosine is float.
        const float ph=static_cast<float>(nco_phase_);
        const float c=std::cos(ph),s=std::sin(ph);
        const IQSample mixed{iq[i].re*c+iq[i].im*s,
                             iq[i].im*c-iq[i].re*s};
        nco_phase_+=2*kPi*carrier_hz_/fs_;
        if(nco_phase_>kPi) nco_phase_-=2*kPi;
        if(nco_phase_<-kPi) nco_phase_+=2*kPi;
        channel_hist_[channel_pos_]=mixed;
        channel_pos_=(channel_pos_+1)%channel_n_;
        float x=0,y=0;
        {
            // Newest sample first. Two straight segments (no per-tap wrap test) and two accumulators per axis to break
            // the floating-point add dependency chain.
            float x0=0,x1=0,y0=0,y1=0;
            std::size_t k=0;
            std::size_t p=channel_pos_;           // taps k=0..channel_pos_-1 read hist[pos-1] down to hist[0]
            for(;k+1<=channel_pos_&&k+2<=channel_pos_;k+=2) {
                const IQSample a=channel_hist_[p-1],b=channel_hist_[p-2];
                x0+=channel_taps_[k]*a.re; y0+=channel_taps_[k]*a.im;
                x1+=channel_taps_[k+1]*b.re; y1+=channel_taps_[k+1]*b.im;
                p-=2;
            }
            for(;k<channel_pos_;++k) { --p; x0+=channel_taps_[k]*channel_hist_[p].re; y0+=channel_taps_[k]*channel_hist_[p].im; }
            p=channel_n_;                          // remaining taps read hist[n-1] down to hist[pos]
            for(;k+2<=channel_n_;k+=2) {
                const IQSample a=channel_hist_[p-1],b=channel_hist_[p-2];
                x0+=channel_taps_[k]*a.re; y0+=channel_taps_[k]*a.im;
                x1+=channel_taps_[k+1]*b.re; y1+=channel_taps_[k+1]*b.im;
                p-=2;
            }
            for(;k<channel_n_;++k) { --p; x0+=channel_taps_[k]*channel_hist_[p].re; y0+=channel_taps_[k]*channel_hist_[p].im; }
            x=x0+x1; y=y0+y1;
        }
        const IQSample filtered{x,y};
        float disc=0;
        if(have_iq_) {
            const float re=filtered.re*prev_iq_.re+filtered.im*prev_iq_.im;
            const float im=filtered.im*prev_iq_.re-filtered.re*prev_iq_.im;
            disc=std::atan2(im,re);
        }
        prev_iq_=filtered;have_iq_=true;
        // Fixed 64-sample update independent of caller chunk boundaries.
        disc_sum_+=disc;disc_sumsq_+=static_cast<double>(disc)*disc;
        if(++disc_count_==64) {
            const double mean=disc_sum_/64.0;
            const double var=disc_sumsq_/64.0-mean*mean;
            if(var<0.1) {
                const double tau=128.0*static_cast<double>(sps_);
                carrier_hz_+=(-std::expm1(-64.0/tau))*mean*fs_/(2*kPi);
                carrier_hz_=std::max(-3000.0,std::min(3000.0,carrier_hz_));
            }
            disc_sum_=0;disc_sumsq_=0;disc_count_=0;
        }
        rrc_hist_[rrc_pos_]=disc;
        rrc_pos_=(rrc_pos_+1)%rrc_n_;
        float rrc=0;
        {
            float r0=0,r1=0,r2=0,r3=0;
            std::size_t k=0,q=rrc_pos_;
            for(;k+4<=rrc_pos_;k+=4) {
                r0+=rrc_taps_[k]*rrc_hist_[q-1]; r1+=rrc_taps_[k+1]*rrc_hist_[q-2];
                r2+=rrc_taps_[k+2]*rrc_hist_[q-3]; r3+=rrc_taps_[k+3]*rrc_hist_[q-4];
                q-=4;
            }
            for(;k<rrc_pos_;++k) { --q; r0+=rrc_taps_[k]*rrc_hist_[q]; }
            q=rrc_n_;
            for(;k+4<=rrc_n_;k+=4) {
                r0+=rrc_taps_[k]*rrc_hist_[q-1]; r1+=rrc_taps_[k+1]*rrc_hist_[q-2];
                r2+=rrc_taps_[k+2]*rrc_hist_[q-3]; r3+=rrc_taps_[k+3]*rrc_hist_[q-4];
                q-=4;
            }
            for(;k<rrc_n_;++k) { --q; r0+=rrc_taps_[k]*rrc_hist_[q]; }
            rrc=(r0+r1)+(r2+r3);
        }
        // Mueller-Muller symbol clock with linear fractional interpolation.
        if(have_rrc_) {
            mm_mu_-=1.0;
            if(mm_mu_<=0) {
                const double frac=std::max(0.0,std::min(1.0,1.0+mm_mu_));
                const float sym=static_cast<float>(prev_rrc_*(1-frac)+rrc*frac);
                if(have_sym_) {
                    const double e=sign(prev_sym_)*sym-sign(sym)*prev_sym_;
                    mm_mu_+=static_cast<double>(sps_)+0.05*e;
                } else {
                    mm_mu_+=static_cast<double>(sps_);have_sym_=true;
                }
                prev_sym_=sym;
                // Residual symbol-domain center/level tracking. Threshold is
                // relative to the recovered four-level eye, not raw volts.
                symbol_dc_+=0.004*(sym-symbol_dc_);
                const double centered=sym-symbol_dc_,a=std::abs(centered);
                if(symbol_abs_==0) symbol_abs_=a;
                else symbol_abs_+=0.015*(a-symbol_abs_);
                const std::uint8_t d=centered>=0 ?
                     (a>symbol_abs_?1U:0U) : (a>symbol_abs_?3U:2U);
                if(sink) sink(d,user);
                ++symbols_;
            }
        }
        prev_rrc_=rrc;have_rrc_=true;
    }
    return true;
}
} // namespace orcsdr::nxdn

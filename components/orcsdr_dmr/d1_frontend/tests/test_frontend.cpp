// Deterministic independent SRRC-shaped 4FSK host vectors; no external IQ data.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "frontend.hpp"
#include "orcsdr_dmr/d0.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

using namespace orcsdr::dmr;
using namespace orcsdr::dmr::d1;
namespace {
constexpr double kPi=3.14159265358979323846;
#define CHECK(c) do {if(!(c)) {std::fprintf(stderr,"CHECK %s:%d %s\n",__FILE__,__LINE__,#c);std::exit(1);}}while(false)
constexpr int kGuard=130;
// Synthesizer independent of the frontend's coefficient array/state.
static double pulse(double t) {
    constexpr double b=.2;
    if (std::abs(t)<1.e-10) return 1.+b*(4./kPi-1.);
    if (std::abs(std::abs(t)-1./(4.*b))<1.e-9)
        return b/std::sqrt(2.)*((1+2/kPi)*std::sin(kPi/(4*b)) +
                                (1-2/kPi)*std::cos(kPi/(4*b)));
    return (std::sin(kPi*(1-b)*t)+4*b*t*std::cos(kPi*(1+b)*t)) /
           (kPi*t*(1-16*b*b*t*t));
}

struct Vector {
    std::vector<std::uint8_t> bits;
    std::vector<ComplexSample> iq;
    double ppm=0;
    double shift=0;
};
Vector make_vector(std::size_t symbols, float cfo, float sigma,
                   double shift, double ppm, bool with_sync, std::uint64_t seed=42,
                   int forced_sync_kind=-1) {
    std::mt19937_64 gen(seed);
    std::uniform_int_distribution<int> uni(0,3);
    Vector result;
    result.ppm=ppm; result.shift=shift;
    result.bits.resize(symbols);
    for(auto& d:result.bits)d=static_cast<std::uint8_t>(uni(gen));
    if(with_sync) {
        for(std::size_t start=90;start+24<symbols;start+=144) {
            const auto kind=forced_sync_kind>=0
                ?static_cast<std::size_t>(forced_sync_kind)
                :(start/144)%kSyncCount;
            const auto sync=kSyncWords[kind];
            for(std::size_t i=0;i<24;i++)
                result.bits[start+i]=static_cast<std::uint8_t>((sync>>(46-2*i))&3U);
        }
    }
    const auto n=static_cast<std::size_t>(kGuard+symbols*10+110);
    std::vector<float> instantaneous(n,cfo);
    double sum=0;
    for(int i=-30;i<=30;++i) sum+=pulse(i/10.);
    const double scalar=10./sum;
    for(std::size_t s=0;s<symbols;++s) {
        const double center=kGuard+shift+static_cast<double>(s)*10.0*(1.0+ppm*1.e-6);
        const int first=std::max(0,static_cast<int>(std::floor(center))-32);
        const int last=std::min(static_cast<int>(n)-1,static_cast<int>(std::floor(center))+32);
        const double magnitude=static_cast<double>(dibit_level(result.bits[s]))*648.0*scalar;
        for(int j=first;j<=last;++j) {
            const double dt=(static_cast<double>(j)-center)/10.0;
            if(std::abs(dt)<=3.0)
                instantaneous[static_cast<std::size_t>(j)] += static_cast<float>(magnitude*pulse(dt));
        }
    }
    std::normal_distribution<float> noise(0.0F,sigma);
    result.iq.reserve(n);
    double phase=0;
    for(auto hz:instantaneous) {
        phase+=2*kPi*hz/48000.0;
        const auto re=static_cast<float>(std::cos(phase));
        const auto im=static_cast<float>(std::sin(phase));
        result.iq.push_back({re+noise(gen),im+noise(gen)});
    }
    return result;
}
struct Run {std::vector<Symbol> syms; Stats stats{};};
Run run(const std::vector<ComplexSample>& iq, std::size_t chunk,
        std::size_t outcap=256, bool cut=false) {
    Frontend demod;
    Run r;
    std::size_t pos=0;
    bool cut_done=false;
    std::array<Symbol,256> output{};
    CHECK(outcap>0 && outcap<=output.size());
    while(pos<iq.size()) {
        if(cut&&!cut_done && pos>=iq.size()/2) {
            demod.discontinuity();cut_done=true;
        }
        const std::size_t count=std::min(chunk,iq.size()-pos);
        const auto x=demod.process(iq.data()+pos,count,output.data(),outcap);
        CHECK(x.consumed>0 && x.consumed<=count);
        CHECK(x.produced<=outcap);
        r.syms.insert(r.syms.end(),output.begin(),output.begin()+static_cast<std::ptrdiff_t>(x.produced));
        pos+=x.consumed;
    }
    r.stats=demod.stats();
    return r;
}
struct Accuracy {std::size_t checked=0,errors=0;};
Accuracy compare(const Vector& v, const Run& r) {
    Accuracy a;
    for(const auto& s:r.syms) {
        CHECK(s.dibit<=3);
        // Output center is delayed by 30 RX FIR samples + ~1 from discriminator.
        const double ix=(static_cast<double>(s.sample_index)-kGuard-v.shift-30.5)/
                        (10.0*(1.0+v.ppm*1.e-6));
        const auto k=static_cast<std::int64_t>(std::llround(ix));
        if(k<0||static_cast<std::size_t>(k)>=v.bits.size())continue;
        ++a.checked;
        if(v.bits[static_cast<std::size_t>(k)]!=s.dibit) ++a.errors;
    }
    return a;
}
void print_case(const char* label, const Vector& v, const Run& r, double max_ber) {
    const auto a=compare(v,r);
    const double ber=a.checked?static_cast<double>(a.errors)/a.checked:1.;
    std::printf("%-18s lock=%llu loss=%llu symbols=%zu compared=%zu errors=%zu SER=%.5f CFO=%+.1fHz corr=%.4f timing=%.6f RMSerr=%.1fHz\n",
        label,static_cast<unsigned long long>(r.stats.locks),
        static_cast<unsigned long long>(r.stats.lost_locks),r.syms.size(),a.checked,a.errors,ber,
        r.stats.carrier_offset_hz,r.stats.sync_correlation,
        r.stats.timing_step_samples,r.stats.decision_rms_hz);
    CHECK(r.stats.locks>=1);
    CHECK(a.checked>v.bits.size()/2);
    CHECK(ber<=max_ber);
}
}

int main() {
    static_assert(kSampleRate==48000 && kSymbolRate==4800 && kSamplesPerSymbol==10);
    CHECK(dibit_level(0)==1 && dibit_level(1)==3 && dibit_level(2)==-1 && dibit_level(3)==-3);
    CHECK(level_dibit(3)==1 && level_dibit(1)==0 && level_dibit(-1)==2 && level_dibit(-3)==3);
    CHECK(Frontend::static_storage_bytes()<8192U);
    // Every D0 SYNC word must lead to the correct metadata label, even
    // though this layer does not claim message CRC or protocol decode.
    for(int kind=0;kind<static_cast<int>(kSyncCount);++kind) {
        const auto v=make_vector(420,320.0F,0.005F,1.1,0.0,true,100+kind,kind);
        const auto rx=run(v.iq,41);
        CHECK(rx.stats.locks>=1 && rx.stats.sync_kind==kind);
        const auto a=compare(v,rx);
        CHECK(a.checked>200 && a.errors<3);
    }
    std::puts("all eight D0 sync variants PASS");
    const auto clean=make_vector(1500,+950.0F,0.005F,0.0,0.0,true);
    const auto basic=run(clean.iq,511);
    print_case("clean +950",clean,basic,0.005);
    CHECK(std::abs(basic.stats.carrier_offset_hz-950.0F)<90.0F);
    CHECK(basic.stats.sync_kind<8U && basic.stats.sync_correlation>0.955F);
    // The phase discriminator must be insensitive to the IQ recorder's
    // arbitrary amplitude scale. A real-air terminator had peak IQ <0.002;
    // our former absolute power gate (1e-5) rejected all its samples.
    for (const auto scale : {1.0e-3F, 1.0e-4F}) {
        auto quiet_iq=clean.iq;
        for(auto& z:quiet_iq) { z.re*=scale; z.im*=scale; }
        const auto quiet=run(quiet_iq,13,1);
        const auto result=compare(clean,quiet);
        CHECK(quiet.stats.invalid_iq==0);
        CHECK(quiet.stats.locks==basic.stats.locks);
        CHECK(quiet.syms.size()==basic.syms.size());
        CHECK(result.checked>clean.bits.size()/2);
        CHECK(result.errors<=result.checked/200);
        for(std::size_t i=0;i<quiet.syms.size();++i)
            CHECK(quiet.syms[i].dibit==basic.syms[i].dibit);
        std::printf("amplitude scale %.4g: locks=%llu IQ-invalid=%llu bits=%zu errors=%zu PASS\\n",
            static_cast<double>(scale),
            static_cast<unsigned long long>(quiet.stats.locks),
            static_cast<unsigned long long>(quiet.stats.invalid_iq),
            result.checked,result.errors);
    }
    const auto chunks=run(clean.iq,1,1);
    CHECK(chunks.syms.size()==basic.syms.size());
    for(std::size_t i=0;i<chunks.syms.size();++i) {
        CHECK(chunks.syms[i].dibit==basic.syms[i].dibit);
        CHECK(chunks.syms[i].sample_index==basic.syms[i].sample_index);
    }
    std::puts("chunk continuity (511 vs 1 samples, output cap=1) PASS");
    const auto shifted=make_vector(1500,-1350.0F,0.025F,3.7,0.0,true);
    const auto s=run(shifted.iq,73,4);
    print_case("shift -1350",shifted,s,0.02);
    CHECK(std::abs(s.stats.carrier_offset_hz+1350.0F)<125.0F);
    const auto noisy=make_vector(1500,+1750.0F,0.065F,7.1,0.0,true);
    const auto nz=run(noisy.iq,97);
    print_case("noisy +1750",noisy,nz,0.06);
    const auto drift=make_vector(3500,-450.0F,0.015F,2.4,350.0,true);
    const auto d=run(drift.iq,111);
    print_case("clock +350ppm",drift,d,0.035);
    const auto negative_drift=make_vector(3500,+200.0F,0.02F,8.6,-350.0,true,45);
    const auto neg=run(negative_drift.iq,13,2);
    print_case("clock -350ppm",negative_drift,neg,0.035);
    const auto far_offset=make_vector(1500,-2700.0F,0.035F,5.1,0.0,true,69);
    const auto far=run(far_offset.iq,211);
    print_case("CFO -2700",far_offset,far,0.035);
    CHECK(std::abs(far.stats.carrier_offset_hz+2700.0F)<130.0F);
    // Proper gap marker MUST clear phase discriminator/clock; reacquisition
    // must derive new lock from later physical sync, not presumed continuity.
    const auto gap=run(clean.iq,173,64,true);
    CHECK(gap.stats.discontinuities==1);
    CHECK(gap.stats.locks>=2);
    CHECK(gap.stats.samples==clean.iq.size());
    std::puts("explicit discontinuity / reacquisition PASS");
    const auto random=make_vector(2500,700.0F,0.02F,1.7,0.0,false);
    const auto false_sig=run(random.iq,243);
    std::printf("false random 4FSK locks=%llu\n",static_cast<unsigned long long>(false_sig.stats.locks));
    CHECK(false_sig.stats.locks==0 && false_sig.syms.empty());
    std::mt19937 gen(299);
    std::normal_distribution<float> white(0.0F,1.0F);
    std::vector<ComplexSample> white_iq(48000);
    for(auto& z:white_iq)z={white(gen),white(gen)};
    const auto false_noise=run(white_iq,251);
    CHECK(false_noise.stats.locks==0 && false_noise.syms.empty());
    std::puts("false white-noise locks=0 PASS");
    // Unmodulated carrier/flat DC discriminator cannot masquerade as DMR.
    std::vector<ComplexSample> cw(24000);
    double phase=0.0;
    for(auto& z:cw) {
        phase+=2*kPi*1250.0/48000.0;
        z={static_cast<float>(std::cos(phase)),static_cast<float>(std::sin(phase))};
    }
    const auto false_cw=run(cw,64);
    CHECK(false_cw.stats.locks==0 && false_cw.syms.empty());
    std::puts("false CW locks=0 PASS");
    // Switching physical sources after a declared gap must not leak CFO.
    Frontend switched;
    std::array<Symbol,256> scratch{};
    auto stream_part=[&](const std::vector<ComplexSample>& data) {
        std::size_t pos=0;
        while(pos<data.size()) {
            const auto n=std::min<std::size_t>(1200,data.size()-pos);
            const auto x=switched.process(data.data()+pos,n,scratch.data(),scratch.size());
            CHECK(x.consumed==n);
            pos+=n;
        }
    };
    stream_part(clean.iq);
    CHECK(switched.stats().locks>=1);
    switched.discontinuity();
    stream_part(shifted.iq);
    CHECK(switched.stats().locks>=2 && switched.stats().discontinuities==1);
    CHECK(std::abs(switched.stats().carrier_offset_hz+1350.0F)<125.0F);
    std::puts("midstream carrier change + declared discontinuity PASS");
    Frontend invalid;
    Symbol symbol{};
    CHECK(invalid.process(nullptr,1,&symbol,1).consumed==0);
    CHECK(invalid.process(clean.iq.data(),50,nullptr,1).consumed==0);
    CHECK(invalid.process(clean.iq.data(),50,&symbol,0).consumed==0);
    auto nan=ComplexSample{std::numeric_limits<float>::quiet_NaN(),0};
    CHECK(invalid.process(&nan,1,&symbol,1).consumed==1);
    CHECK(invalid.stats().invalid_iq==1);
    invalid.discontinuity();
    CHECK(invalid.stats().discontinuities==1);
    invalid.reset();
    CHECK(invalid.stats().samples==0 && invalid.stats().discontinuities==0);
    std::puts("invalid inputs, reset and fixed-memory API PASS");
    // Throughput over 1.5M samples; exclude waveform synthesis and allocations.
    // Reset for each 15,240-sample repeated capture for deterministic timing.
    Frontend bench;
    std::array<Symbol,2048> bench_out{};
    constexpr std::size_t repetitions=100;
    const auto begin=std::chrono::steady_clock::now();
    for(std::size_t repeat=0;repeat<repetitions;++repeat) {
        bench.reset();
        const auto x=bench.process(clean.iq.data(),clean.iq.size(),bench_out.data(),bench_out.size());
        CHECK(x.consumed==clean.iq.size() && x.produced==basic.syms.size());
    }
    const auto end=std::chrono::steady_clock::now();
    const auto sec=std::chrono::duration<double>(end-begin).count();
    const auto samples=clean.iq.size()*repetitions;
    std::printf("PERF: %zu IQ samples in %.6fs = %.3f Msamples/s (%.2fx real-time), Frontend=%zu bytes\n",
      samples,sec,samples/sec/1.e6,samples/sec/48000.0,Frontend::static_storage_bytes());
    std::puts("DMR D1 host suite PASS");
}

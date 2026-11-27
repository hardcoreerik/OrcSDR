// D1-P host-only first-burst IQ tests: never seed pre-roll with an oracle.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "frontend.hpp"
#include "orcsdr_dmr/d0.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

using namespace orcsdr::dmr;
using namespace orcsdr::dmr::d1;
namespace {
constexpr double pi=3.14159265358979323846;
#define REQUIRE(c) do {if(!(c)){std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c);std::exit(1);}}while(false)

// Independent transmitter generates symbol-shaped instantaneous FSK deviation;
// no front-end internals, state or cached dibits are consulted.
double pulse(double t) {
    constexpr double b=.2;
    if (std::abs(t)<1.e-10) return 1.+b*(4./pi-1.);
    if (std::abs(std::abs(t)-1./(4.*b))<1.e-9)
        return b/std::sqrt(2.)*((1+2/pi)*std::sin(pi/(4*b)) +
                                (1-2/pi)*std::cos(pi/(4*b)));
    return (std::sin(pi*(1-b)*t)+4*b*t*std::cos(pi*(1+b)*t)) /
           (pi*t*(1-16*b*b*t*t));
}
struct Capture { std::vector<std::uint8_t> bits; std::vector<ComplexSample> iq; double shift=0; double ppm=0; int guard=130; };
Capture make_single(float carrier, float noise, double shift, double ppm,
                    std::uint64_t seed=112, unsigned kind=1, int guard=130,
                    bool include_sync=true) {
    Capture cap;
    cap.guard=guard;cap.shift=shift;cap.ppm=ppm;
    cap.bits.resize(132);
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> uniform(0,3);
    for(auto& d:cap.bits) d=static_cast<std::uint8_t>(uniform(rng));
    if(include_sync) {
        const auto sync=kSyncWords[kind];
        for(unsigned i=0;i<24;++i)
            cap.bits[54+i]=static_cast<std::uint8_t>((sync>>(46-2*i))&3U);
    }
    const auto n=static_cast<std::size_t>(guard+132*10+120);
    std::vector<float> freq(n,carrier);
    double total=0;
    for(int j=-30;j<=30;++j) total+=pulse(j/10.);
    for(std::size_t i=0;i<cap.bits.size();++i) {
        const double center=guard+shift+i*10.0*(1.0+ppm*1e-6);
        const int first=std::max(0,static_cast<int>(std::floor(center))-32);
        const int last=std::min(static_cast<int>(n)-1,static_cast<int>(std::floor(center))+32);
        const double amplitude=648.0 * static_cast<double>(dibit_level(cap.bits[i])) *10.0/total;
        for(int j=first;j<=last;++j) {
            const double dt=(j-center)/10.0;
            if(std::abs(dt)<=3.0) freq[static_cast<std::size_t>(j)] +=
                static_cast<float>(amplitude*pulse(dt));
        }
    }
    std::normal_distribution<float> gaussian(0.0f,noise);
    cap.iq.reserve(n);
    double phase=0;
    for(float f:freq){
        phase+=2*pi*f/48000.0;
        cap.iq.push_back({static_cast<float>(std::cos(phase))+gaussian(rng),
                          static_cast<float>(std::sin(phase))+gaussian(rng)});
    }
    return cap;
}
struct Run { std::vector<Symbol> bits; Stats stats; };
Run receive(const std::vector<ComplexSample>& iq,std::size_t chunk,std::size_t capacity) {
    Frontend receiver;
    Run r;
    std::array<Symbol,256> output{};
    REQUIRE(chunk>0&&capacity>0&&capacity<=output.size());
    for(std::size_t index=0;index<iq.size();) {
        const auto step=receiver.process(iq.data()+index,
                                         std::min(chunk,iq.size()-index),
                                         output.data(),capacity);
        REQUIRE(step.consumed>0&&step.produced<=capacity);
        index+=step.consumed;
        r.bits.insert(r.bits.end(),output.begin(),output.begin()+
                      static_cast<std::ptrdiff_t>(step.produced));
    }
    r.stats=receiver.stats();
    return r;
}
// Recover actual first-burst symbol center from returned RX FIR timestamps.
struct Accuracy {std::size_t checked=0,errors=0;};
Accuracy evaluate(const Capture& tx,const Run& rx) {
    Accuracy a;
    for(auto s:rx.bits) {
        REQUIRE(s.dibit<=3);
        const auto index=static_cast<std::int64_t>(std::llround(
            (static_cast<double>(s.sample_index)-tx.guard-tx.shift-30.5) /
            (10.0*(1.+tx.ppm*1e-6))));
        if(index<0||index>=static_cast<std::int64_t>(tx.bits.size()))continue;
        ++a.checked;
        if(tx.bits[static_cast<std::size_t>(index)]!=s.dibit)++a.errors;
    }
    return a;
}
void test_case(const char* name,float cfo,float sigma,double shift,double ppm,
               unsigned kind,std::size_t error_limit,bool require_perfect_sync=true) {
    const auto tx=make_single(cfo,sigma,shift,ppm,112+kind,kind);
    const auto large=receive(tx.iq,511,256);
    const auto smallest=receive(tx.iq,1,1);
    const auto tiny=receive(tx.iq,7,1);
    REQUIRE(large.stats.full_preroll_acquisitions==1);
    REQUIRE(large.stats.preroll_symbols==78);
    REQUIRE(large.stats.sync_kind==kind);
    REQUIRE(large.bits.size()>=132);
    REQUIRE(large.bits.size()==smallest.bits.size());
    REQUIRE(large.bits.size()==tiny.bits.size());
    for(std::size_t i=0;i<large.bits.size();++i){
        REQUIRE(large.bits[i].dibit==smallest.bits[i].dibit);
        REQUIRE(large.bits[i].sample_index==smallest.bits[i].sample_index);
        REQUIRE(large.bits[i].dibit==tiny.bits[i].dibit);
        REQUIRE(large.bits[i].sample_index==tiny.bits[i].sample_index);
        if(i) REQUIRE(large.bits[i].sample_index>large.bits[i-1].sample_index);
    }
    const auto a=evaluate(tx,large);
    // Exactly 132 first-burst symbols are required (not merely many symbols
    // from a later frame). No random preamble symbol may be included.
    REQUIRE(a.checked==132);
    std::fprintf(stderr,"CASE %s errors=%zu limit=%zu first=%llu last=%llu symbols=%zu\n",name,a.errors,error_limit,static_cast<unsigned long long>(large.bits.front().sample_index),static_cast<unsigned long long>(large.bits[131].sample_index),large.bits.size());
    REQUIRE(a.errors<=error_limit);
    // 24 sync dibits MUST have been actually sliced and retained, in order.
    if(require_perfect_sync) for(unsigned i=0;i<24;++i) REQUIRE(large.bits[54+i].dibit==tx.bits[54+i]);
    std::printf("%s first_burst=132 preroll=78 error=%zu/132 CFO=%.0fHz correlation=%.4f bytes=%zu\n",
                name,a.errors,large.stats.carrier_offset_hz,large.stats.sync_correlation,
                Frontend::static_storage_bytes());
}
void test_no_fabrication(){
    // A partial IQ capture starts at the burst's own first symbol and lacks
    // the full 61-tap receive support for its earliest payload. A 24-symbol
    // sync is still present, but must NOT trigger synthetic pre-roll.
    const auto truncated=make_single(450.0F,0.004F,0.,0.,456,1,0);
    const auto rx=receive(truncated.iq,47,1);
    REQUIRE(rx.stats.full_preroll_acquisitions==0);
    REQUIRE(rx.stats.locks==0);
    REQUIRE(rx.bits.empty());
    // An otherwise complete transmission whose *capture* starts after the
    // first 15 payload symbols likewise has no recoverable first burst.
    const auto complete=make_single(-620.0F,0.003F,1.1,0.,457,1);
    const std::size_t crop=static_cast<std::size_t>(complete.guard+150);
    const std::vector<ComplexSample> missing(complete.iq.begin()+static_cast<std::ptrdiff_t>(crop),
                                             complete.iq.end());
    const auto mid=receive(missing,17,1);
    REQUIRE(mid.stats.full_preroll_acquisitions==0);
    REQUIRE(mid.bits.empty());
    std::puts("no fabricated startup/mid-burst pre-roll PASS");
}
void test_false_locks(){
    const auto random=make_single(-750.0F,0.01F,2.7,0.,777,1,130,false);
    auto result=receive(random.iq,97,4);
    REQUIRE(result.stats.locks==0 && result.bits.empty());
    std::mt19937 rng(91);
    std::normal_distribution<float> normal(0.0F,1.0F);
    std::vector<ComplexSample> noise(48000);
    for(auto& sample:noise)sample={normal(rng),normal(rng)};
    result=receive(noise,128,1);
    REQUIRE(result.stats.locks==0&&result.bits.empty());
    std::puts("random FSK + AWGN false-lock fixtures PASS");
}
void test_backpressure_flush_and_cut(){
    const auto tx=make_single(750.0F,0.012F,1.5,0.,950,2);
    Frontend rx;
    std::array<Symbol,1> out{};
    // A full output buffer disables IQ consumption, including pre-roll.
    REQUIRE(rx.process(tx.iq.data(),tx.iq.size(),out.data(),0).consumed==0);
    REQUIRE(rx.stats().samples==0);
    REQUIRE(rx.process(nullptr,1,out.data(),1).consumed==0);
    std::size_t pos=0;
    while(pos<tx.iq.size() && rx.stats().preroll_symbols==0) {
        const auto r=rx.process(tx.iq.data()+pos,std::min<std::size_t>(8,tx.iq.size()-pos),out.data(),1);
        REQUIRE(r.consumed>=1 && r.consumed<=8);
        REQUIRE(r.produced<=1);
        pos+=r.consumed;
    }
    REQUIRE(rx.stats().locks==1);
    REQUIRE(rx.stats().preroll_symbols==1);
    const auto first_time=out[0].sample_index;
    rx.discontinuity();
    REQUIRE(rx.stats().lost_locks>=1);
    REQUIRE(!rx.stats().locked);
    REQUIRE(rx.stats().preroll_symbols==1);
    std::uint64_t extra=0;
    while(pos<tx.iq.size()) {
        const auto r=rx.process(tx.iq.data()+pos,std::min<std::size_t>(8,tx.iq.size()-pos),out.data(),1);
        REQUIRE(r.consumed>0);
        extra+=r.produced;
        pos+=r.consumed;
    }
    REQUIRE(extra==0); // Neither queued nor synthetic symbols survive cut.
    REQUIRE(first_time>0);
    std::puts("one-symbol bridge pacing / interrupted pre-roll PASS");
}
void test_reset(){
    const auto tx=make_single(320.0F,0.008F,1.1,0.,800,3);
    Frontend rx;
    std::array<Symbol,8> out{};
    std::size_t pos=0;
    while(pos<tx.iq.size()) {
        const auto r=rx.process(tx.iq.data()+pos,std::min<std::size_t>(71,tx.iq.size()-pos),out.data(),out.size());
        REQUIRE(r.consumed>0);pos+=r.consumed;
    }
    REQUIRE(rx.stats().full_preroll_acquisitions==1);
    const auto total=rx.stats().preroll_symbols;
    rx.discontinuity();
    REQUIRE(rx.stats().discontinuities==1);
    REQUIRE(!rx.stats().locked);
    REQUIRE(rx.stats().preroll_symbols==total);
    pos=0;
    while(pos<tx.iq.size()) {
        const auto r=rx.process(tx.iq.data()+pos,std::min<std::size_t>(71,tx.iq.size()-pos),out.data(),out.size());
        REQUIRE(r.consumed>0);pos+=r.consumed;
    }
    REQUIRE(rx.stats().full_preroll_acquisitions==2);
    REQUIRE(rx.stats().preroll_symbols==156);
    rx.reset();
    REQUIRE(rx.stats().samples==0 && rx.stats().symbols==0 && rx.stats().preroll_symbols==0);
    const auto nan=ComplexSample{std::numeric_limits<float>::quiet_NaN(),1.0F};
    REQUIRE(rx.process(&nan,1,out.data(),out.size()).consumed==1);
    REQUIRE(rx.stats().invalid_iq==1);
    std::puts("discontinuity/reset/invalid IQ PASS");
}
} // namespace

int main(){
    // Frontend + existing bridge must remain under the old ~8 KiB budget.
    REQUIRE(Frontend::static_storage_bytes()<6000);
    REQUIRE(kSymbolRate==4800&&kSampleRate==48000);
    for(unsigned kind=0;kind<8;++kind)
        test_case("all sync kinds",320.0F,0.004F,1.1,0.,kind,2);
    test_case("CFO +1750 noise",1750.0F,0.06F,7.1,0.,1,6);
    test_case("CFO -2700 shift",-2700.0F,0.03F,5.1,0.,5,6);
    test_case("clock +350ppm",950.0F,0.015F,3.1,350.,3,5);
    test_case("clock -350ppm",-1350.0F,0.02F,8.6,-350.,7,8,false);
    test_no_fabrication();
    test_false_locks();
    test_reset();
    test_backpressure_flush_and_cut();
    std::printf("D1-P PASS Frontend %zu bytes (x86_64)\n",Frontend::static_storage_bytes());
}

// D1-C host regression: real frontend->protocol path, synthesized IQ only.
// No external IQ or decoded audio; not an on-air receiver acceptance test.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/d1_bridge.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

using namespace orcsdr::dmr;
namespace bridge = orcsdr::dmr::d1::integration;
using orcsdr::dmr::d1::ComplexSample;

#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x); std::abort(); \
} } while(false)

namespace {
constexpr double kPi = 3.14159265358979323846;
using Burst = std::array<std::uint8_t,132>;
using Bytes12 = std::array<std::uint8_t,12>;
constexpr std::uint16_t kGolayP[8] = {0x3da,0xd99,0x6cd,0x367,0xdc6,0xa97,0x93e,0x8eb};
constexpr std::uint8_t kH15[11] = {9,13,15,14,7,10,5,11,12,6,3};
constexpr std::uint8_t kH13[9] = {15,14,7,10,5,11,12,6,3};
constexpr std::uint8_t kRs[9][3] = {
    {0x1c,0xbc,0xfd},{0x89,0x31,0x08},{0xad,0x41,0x36},
    {0x7d,0x71,0x16},{0xf3,0xa6,0x3a},{0x08,0x83,0x7b},
    {0x3f,0x6f,0x02},{0x6c,0x0d,0xa7},{0x0e,0x38,0x40}};

void set_bit(std::uint8_t* data, std::size_t bit, bool one) {
    const auto mask = static_cast<std::uint8_t>(1U << (7U - bit%8U));
    if(one) data[bit/8U] |= mask;
    else data[bit/8U] &= static_cast<std::uint8_t>(~mask);
}
bool get_bit(const std::uint8_t* data, std::size_t bit) {
    return (data[bit/8U] & (1U << (7U - bit%8U))) != 0U;
}
unsigned parity(const std::uint8_t* bits, unsigned count, const std::uint8_t* mask) {
    unsigned x=0; for(unsigned i=0;i<count;++i) if(bits[i]) x ^= mask[i]; return x;
}
std::uint8_t gf(std::uint8_t a, std::uint8_t b) {
    std::uint8_t out=0;
    for(unsigned i=0;i<8;++i){
        if(b & 1U) out ^= a;
        b >>= 1U;
        const bool hi=(a & 0x80U)!=0U;
        a=static_cast<std::uint8_t>((a << 1U) ^ (hi ? 0x1dU : 0U));
    }
    return out;
}
Bytes12 make_lc(std::uint32_t dest, std::uint32_t src, unsigned type=1) {
    Bytes12 d{};
    d[0]=0; d[1]=0; d[2]=3;
    d[3]=static_cast<std::uint8_t>(dest>>16U);
    d[4]=static_cast<std::uint8_t>(dest>>8U);
    d[5]=static_cast<std::uint8_t>(dest);
    d[6]=static_cast<std::uint8_t>(src>>16U);
    d[7]=static_cast<std::uint8_t>(src>>8U);
    d[8]=static_cast<std::uint8_t>(src);
    for(unsigned j=0;j<3;++j){
        unsigned v=0;
        for(unsigned i=0;i<9;++i) v ^= gf(d[i],kRs[i][j]);
        d[9+j]=static_cast<std::uint8_t>(v ^ (type==1 ? 0x96U:0x99U));
    }
    return d;
}
BptcWire make_bptc(const Bytes12& src) {
    std::uint8_t mat[13][15]{};
    for(unsigned i=0;i<96;++i) mat[(i+3U)/11U][(i+3U)%11U]=get_bit(src.data(),i);
    for(unsigned row=0;row<9;++row){
        const auto par=parity(mat[row],11,kH15);
        for(unsigned j=0;j<4;++j)mat[row][11+j]=static_cast<std::uint8_t>((par>>(3U-j))&1U);
    }
    for(unsigned col=0;col<15;++col){
        std::uint8_t column[9]{};
        for(unsigned row=0;row<9;++row)column[row]=mat[row][col];
        const auto par=parity(column,9,kH13);
        for(unsigned j=0;j<4;++j)mat[9+j][col]=static_cast<std::uint8_t>((par>>(3U-j))&1U);
    }
    BptcWire src_bits{},tx{};
    for(unsigned row=0;row<13;++row)for(unsigned col=0;col<15;++col)
        set_bit(src_bits.data(),1+row*15+col,mat[row][col]!=0);
    CHECK(bptc_interleave(src_bits,tx));
    return tx;
}
std::uint32_t make_golay(std::uint8_t code){
    unsigned p=0;
    for(unsigned j=0;j<8;++j)if(code&(0x80U>>j))p ^= kGolayP[j];
    return (static_cast<std::uint32_t>(code)<<12U)|p;
}
Burst make_burst(const Bytes12& lc,SyncKind kind,unsigned cc,unsigned dtype){
    std::array<std::uint8_t,264> bits{};
    const auto bptc=make_bptc(lc);
    for(unsigned i=0;i<196;++i){
        const unsigned pos=i<98U?i:i+68U;
        bits[pos]=static_cast<std::uint8_t>(get_bit(bptc.data(),i));
    }
    const auto st=make_golay(static_cast<std::uint8_t>((cc<<4U)|dtype));
    for(unsigned i=0;i<10;++i)bits[98+i]=static_cast<std::uint8_t>((st>>(19U-i))&1U);
    for(unsigned i=0;i<10;++i)bits[156+i]=static_cast<std::uint8_t>((st>>(9U-i))&1U);
    const auto sync=sync_word(kind);
    for(unsigned i=0;i<48;++i)bits[108+i]=static_cast<std::uint8_t>((sync>>(47U-i))&1U);
    Burst result{};
    for(unsigned i=0;i<132;++i) result[i]=static_cast<std::uint8_t>((bits[2*i]<<1U)|bits[2*i+1]);
    return result;
}
std::array<std::uint8_t,12> cach(unsigned slot){
    CachFields fields{};fields.tdma_channel=slot==2U;
    CachWire w{};
    CHECK(pack_cach(fields,w));
    std::array<std::uint8_t,12> out{};
    for(unsigned i=0;i<12;++i)
        out[i]=static_cast<std::uint8_t>((w[i/4U]>>(6U-2U*(i%4U)))&3U);
    return out;
}
std::vector<std::uint8_t> make_stream(unsigned bursts=9) {
    std::vector<std::uint8_t> dibits;
    dibits.reserve(static_cast<std::size_t>(bursts)*144U);
    for(unsigned i=0;i<bursts;++i){
        const auto ca=cach(1U+i%2U);
        const auto b=make_burst(make_lc(99,3024109),SyncKind::bs_data,1,1);
        dibits.insert(dibits.end(),ca.begin(),ca.end());
        dibits.insert(dibits.end(),b.begin(),b.end());
    }
    return dibits;
}
double pulse(double t) {
    constexpr double b=.2;
    if(std::abs(t)<1.e-10)return 1.+b*(4./kPi-1.);
    if(std::abs(std::abs(t)-1./(4.*b))<1.e-9)
        return b/std::sqrt(2.)*((1+2./kPi)*std::sin(kPi/(4.*b))+
                                 (1-2./kPi)*std::cos(kPi/(4.*b)));
    return (std::sin(kPi*(1.-b)*t)+4.*b*t*std::cos(kPi*(1.+b)*t))/
           (kPi*t*(1.-16.*b*b*t*t));
}
std::vector<ComplexSample> synthesize(const std::vector<std::uint8_t>& dibits,
                                      double cfo=450.,double sigma=.005,
                                      std::uint64_t seed=21) {
    constexpr int guard=130;
    const std::size_t count=static_cast<std::size_t>(guard)+dibits.size()*10U+110U;
    std::vector<float> freq(count,static_cast<float>(cfo));
    double sum=0;for(int i=-30;i<=30;++i)sum+=pulse(static_cast<double>(i)/10.);
    const double scale=10./sum;
    for(std::size_t s=0;s<dibits.size();++s){
        const double center=guard+static_cast<double>(s)*10.;
        const auto first=std::max(0,static_cast<int>(center)-32);
        const auto last=std::min(static_cast<int>(count)-1,static_cast<int>(center)+32);
        const double magnitude=static_cast<double>(d1::dibit_level(dibits[s]))*648.*scale;
        for(int k=first;k<=last;++k){
            const double dt=(static_cast<double>(k)-center)/10.;
            if(std::abs(dt)<=3.) freq[static_cast<std::size_t>(k)] +=
                static_cast<float>(magnitude*pulse(dt));
        }
    }
    std::mt19937_64 generator(seed);
    std::normal_distribution<float> noise(0.0F,static_cast<float>(sigma));
    std::vector<ComplexSample> iq; iq.reserve(count);
    double phase=0;
    for(const auto f:freq){
        phase+=2.*kPi*f/48000.;
        iq.push_back({static_cast<float>(std::cos(phase))+noise(generator),
                      static_cast<float>(std::sin(phase))+noise(generator)});
    }
    return iq;
}
struct Capture {
    std::vector<bridge::Event> events;
    bridge::Stats bridge_stats{};
    d1::Stats frontend_stats{};
};
void collect(const bridge::Event& event,void* user) noexcept {
    static_cast<std::vector<bridge::Event>*>(user)->push_back(event);
}
Capture run(const std::vector<ComplexSample>& iq,std::size_t chunk){
    bridge::Receiver rx;
    Capture cap;
    for(std::size_t pos=0;pos<iq.size();){
        const auto num=std::min(chunk,iq.size()-pos);
        const auto r=rx.process(iq.data()+pos,num,collect,&cap.events);
        CHECK(r.ok && r.consumed==num);
        pos+=num;
    }
    cap.bridge_stats=rx.stats();cap.frontend_stats=rx.frontend_stats();
    return cap;
}
unsigned identities(const Capture& cap){
    unsigned count=0;
    for(const auto& e:cap.events){
        if(e.decoded.evidence==d1::Evidence::identity_valid) {
            CHECK(e.decoded.integrity_valid);
            CHECK(e.decoded.destination_id==99);
            CHECK(e.decoded.source_id==3024109);
            CHECK(e.decoded.colour_code==1);
            // D1-S intentionally leaves the first three BS CACH slots
            // unknown. Identity may be independently RS-verified.
            if (e.decoded.slot_from_cach) {
                CHECK(e.decoded.slot==1 || e.decoded.slot==2);
                CHECK(e.decoded.cach_present);
                CHECK(e.decoded.cach_confidence==d1::CachConfidence::cadence_validated);
                CHECK(e.decoded.cach_cadence_bursts>=4);
            } else {
                CHECK(e.decoded.slot==0);
                CHECK(!e.decoded.cach_present);
            }
            CHECK(e.sample_time_known);
            ++count;
        }
    }
    return count;
}
void test_first_burst_recovery(){
    // One complete direct-mode burst with no preceding BS CACH; the measured
    // acquisition payload must produce independent full-LC identity evidence.
    const auto burst=make_burst(make_lc(99,3024109),SyncKind::direct_ts1_data,1,1);
    const std::vector<std::uint8_t> bits(burst.begin(),burst.end());
    const auto iq=synthesize(bits,320.,.001);
    const auto one=run(iq,1);
    const auto bulk=run(iq,iq.size());
    CHECK(one.bridge_stats.acquisitions==1);
    CHECK(one.frontend_stats.full_preroll_acquisitions==1);
    CHECK(one.bridge_stats.acquisition_prerolls==1);
    CHECK(one.bridge_stats.acquisition_bursts_missed==0);
    CHECK(one.frontend_stats.preroll_symbols==78);
    std::fprintf(stderr,
        "FIRST BURST DIAGNOSTIC one=%zu bulk=%zu dibits=%llu "
        "locks=%llu loss=%llu prerolls=%llu pre_syms=%llu "
        "statAcq=%llu statMiss=%llu\n",
        one.events.size(), bulk.events.size(),
        static_cast<unsigned long long>(one.bridge_stats.dibits_sent),
        static_cast<unsigned long long>(one.frontend_stats.locks),
        static_cast<unsigned long long>(one.frontend_stats.lost_locks),
        static_cast<unsigned long long>(one.frontend_stats.full_preroll_acquisitions),
        static_cast<unsigned long long>(one.frontend_stats.preroll_symbols),
        static_cast<unsigned long long>(one.bridge_stats.acquisitions),
        static_cast<unsigned long long>(one.bridge_stats.acquisition_bursts_missed));
    if (one.events.empty() || bulk.events.empty()) {
        d1::Frontend raw_front;
        std::vector<std::uint8_t> decisions;
        for (std::size_t p=0;p<iq.size();) {
            d1::Symbol sy{};
            const auto step=raw_front.process(iq.data()+p,
                std::min<std::size_t>(8,iq.size()-p),&sy,1);
            CHECK(step.consumed>0);
            p+=step.consumed;
            if(step.produced) decisions.push_back(sy.dibit);
        }
        std::size_t sync_bad=0;
        std::uint64_t sync_wire=0;
        for(std::size_t k=54;k<78 && k<decisions.size();++k) {
            if (decisions[k]!=bits[12+k]) ++sync_bad;
            sync_wire=(sync_wire<<2U)|decisions[k];
        }
        std::size_t full_bad=0;
        for(std::size_t k=0;k<132 && k<decisions.size();++k)
            if(decisions[k]!=bits[12+k]) ++full_bad;
        std::fprintf(stderr,
            "FIRST BURST DIBIT DIAGNOSTIC count=%zu mismatch=%zu sync_bad=%zu "
            "wire=%012llx expected=%012llx\n",
            decisions.size(),full_bad,sync_bad,
            static_cast<unsigned long long>(sync_wire),
            static_cast<unsigned long long>(sync_word(SyncKind::bs_data)));
    }
    CHECK(one.events.size()==1 && bulk.events.size()==1);
    CHECK(identities(one)==1);
    CHECK(one.events[0].decoded.slot==1); // Explicit direct-mode TS1 sync.
    CHECK(!one.events[0].decoded.slot_from_cach);
    CHECK(one.events[0].decoded.cach_confidence==d1::CachConfidence::none);
    CHECK(one.events[0].decoded.burst_start_dibit==0);
    CHECK(one.events[0].sample_time_known);
    CHECK(one.events[0].burst_start_sample==bulk.events[0].burst_start_sample);
    CHECK(one.events[0].decoded.source_id==bulk.events[0].decoded.source_id);
    std::puts("synthetic first complete DMR burst: valid ID, unknown BS slot");
}
void test_stream_alignment(){
    const auto bits=make_stream();
    const auto iq=synthesize(bits);
    const auto full=run(iq,iq.size());
    const auto single=run(iq,1);
    CHECK(full.frontend_stats.locks>=1);
    CHECK(full.bridge_stats.acquisitions>=1);
    CHECK(full.bridge_stats.acquisition_prerolls>=1);
    CHECK(full.bridge_stats.acquisition_bursts_missed==0);
    CHECK(identities(full)>=8); // Measured first-burst payload is now forwarded.
    CHECK(full.events.front().decoded.burst_start_dibit==0);
    CHECK(full.bridge_stats.validated_identities==identities(full));
    CHECK(full.events.size()==single.events.size());
    CHECK(full.bridge_stats.dibits_sent==single.bridge_stats.dibits_sent);
    for(std::size_t i=0;i<full.events.size();++i){
        const auto& a=full.events[i];const auto& b=single.events[i];
        CHECK(a.decoded.evidence==b.decoded.evidence);
        CHECK(a.decoded.burst_start_dibit==b.decoded.burst_start_dibit);
        CHECK(a.decoded.slot==b.decoded.slot);
        CHECK(a.decoded.source_id==b.decoded.source_id);
        CHECK(a.burst_start_sample==b.burst_start_sample);
    }
    std::printf("synthetic IQ: locks=%llu, bursts=%zu, valid IDs=%llu\n",
                static_cast<unsigned long long>(full.frontend_stats.locks),
                full.events.size(),
                static_cast<unsigned long long>(full.bridge_stats.validated_identities));
}
void test_reset_and_discontinuity(){
    const auto iq=synthesize(make_stream());
    bridge::Receiver rx;
    std::vector<bridge::Event> events;
    CHECK(!rx.process(nullptr,100,collect,&events).ok);
    CHECK(rx.stats().invalid_calls==1);
    const auto half=iq.size()/2;
    auto r=rx.process(iq.data(),half,collect,&events);
    CHECK(r.ok && r.consumed==half);
    const auto before=rx.stats().acquisitions;
    CHECK(before>=1);
    rx.discontinuity();
    CHECK(rx.stats().stream_epoch==1);
    CHECK(rx.protocol_dibits()==0);
    r=rx.process(iq.data(),iq.size(),collect,&events);
    CHECK(r.ok && r.consumed==iq.size());
    CHECK(rx.stats().acquisitions>before);
    bool has_epoch1=false;
    for(const auto& e:events)if(e.stream_epoch==1)has_epoch1=true;
    CHECK(has_epoch1);
    rx.reset();
    CHECK(rx.stats().acquisitions==0 && rx.stats().stream_epoch==0);
    CHECK(rx.protocol_dibits()==0);
}
void test_noise_and_empty(){
    bridge::Receiver rx;
    CHECK(rx.process(nullptr,0).ok);
    std::vector<ComplexSample> empty(48000,{0.0F,0.0F});
    std::vector<bridge::Event> events;
    const auto x=rx.process(empty.data(),empty.size(),collect,&events);
    CHECK(x.ok && x.consumed==empty.size() && x.events==0);
    CHECK(events.empty() && rx.stats().validated_identities==0);
}
}

int main(){
    CHECK(bridge::Receiver::static_storage_bytes()<8192U);
    test_first_burst_recovery();
    test_stream_alignment();
    test_reset_and_discontinuity();
    test_noise_and_empty();
    std::puts("dmr_d1_integration_tests: PASS (synthetic IQ only)");
}

// D1-F: independent ETSI-framed adversarial hard-dibit audit.
// No production decoder algorithm, source, constants or third-party code copied.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/d1.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

using orcsdr::dmr::d1::Decoder;
using orcsdr::dmr::d1::Event;
using orcsdr::dmr::d1::Evidence;
using orcsdr::dmr::d1::IdentityKind;
using orcsdr::dmr::d1::CachConfidence;
using orcsdr::dmr::SyncKind;
namespace {
constexpr std::uint64_t kSeed = 0xD1F20261111ULL;
constexpr std::array<std::uint64_t, 4> kDataSync{{
    0xDFF57D75DF5DULL, 0xD5D7F77FD757ULL,
    0xF7FDD5DDFD55ULL, 0xD7557F5FF7F5ULL
}};
constexpr std::array<std::uint64_t, 4> kVoiceSync{{
    0x755FD7DF75F7ULL, 0x7F7D5DD57DFDULL,
    0x5D577F7757FFULL, 0x7DFFD5F55D5FULL
}};
constexpr std::array<SyncKind, 4> kDataKind{{
    SyncKind::bs_data, SyncKind::ms_data,
    SyncKind::direct_ts1_data, SyncKind::direct_ts2_data
}};
constexpr std::array<SyncKind, 4> kVoiceKind{{
    SyncKind::bs_voice, SyncKind::ms_voice,
    SyncKind::direct_ts1_voice, SyncKind::direct_ts2_voice
}};
// From ETSI TS 102 361-1 Table B.11, systematic Golay(20,8).
constexpr std::array<std::uint16_t, 8> kGolayRows{{
    0x3DA, 0xD99, 0x6CD, 0x367, 0xDC6, 0xA97, 0x93E, 0x8EB
}};
// Independently transcribed systematic BPTC row/column parity checks.
constexpr std::array<std::uint8_t, 11> kRow{{9,13,15,14,7,10,5,11,12,6,3}};
constexpr std::array<std::uint8_t, 9> kColumn{{15,14,7,10,5,11,12,6,3}};
// ETSI TS 102 361-1 B.18: systematic RS(12,9) parity generator.
constexpr std::uint8_t kRs[9][3] = {
    {0x1c,0xbc,0xfd}, {0x89,0x31,0x08}, {0xad,0x41,0x36},
    {0x7d,0x71,0x16}, {0xf3,0xa6,0x3a}, {0x08,0x83,0x7b},
    {0x3f,0x6f,0x02}, {0x6c,0x0d,0xa7}, {0x0e,0x38,0x40}
};
using Burst = std::array<std::uint8_t, 132>;
using Data = std::array<std::uint8_t, 12>;
struct Metrics {
    std::uint64_t examined_bits=0, candidates=0, events=0, tentative=0;
    std::uint64_t identities=0, false_identities=0;
    std::uint64_t one_bit=0, two_bit=0, three_bit=0, high_bit=0;
    std::uint64_t rejected_bad_integrity=0, rejected_voice=0;
} metrics;

[[noreturn]] void fail(const char* expr, int line) {
    std::fprintf(stderr, "FAIL D1-F:%d: %s\n",line,expr);
    std::exit(1);
}
#define REQUIRE(x) do { if (!(x)) fail(#x, __LINE__); } while(false)

bool getbit(const std::uint8_t* p, unsigned i) {
    return ((p[i/8U] >> (7U-i%8U)) & 1U) != 0U;
}
void setbit(std::uint8_t* p, unsigned i, bool v) {
    const auto m=static_cast<std::uint8_t>(1U << (7U-i%8U));
    if(v) p[i/8U] |= m;
    else p[i/8U] &= static_cast<std::uint8_t>(~m);
}
unsigned popcount(std::uint64_t x) {
    unsigned n=0;
    for (; x; x &= (x-1U)) ++n;
    return n;
}
std::uint32_t golay(std::uint8_t u) {
    std::uint16_t p=0;
    for(unsigned i=0;i<8;++i) if(u & (0x80U>>i)) p^=kGolayRows[i];
    return (static_cast<std::uint32_t>(u)<<12U)|p;
}
std::uint8_t gf(std::uint8_t a,std::uint8_t b) {
    std::uint8_t z=0;
    while(b) {
        if (b&1U) z^=a;
        const bool carry=(a&0x80U)!=0U;
        a=static_cast<std::uint8_t>((a<<1U)^(carry?0x1dU:0U));
        b=static_cast<std::uint8_t>(b>>1U);
    }
    return z;
}
Data full_lc(unsigned type, unsigned flco=0) {
    Data p{};
    p[0]=static_cast<std::uint8_t>(flco);
    p[1]=0;
    p[2]=3;
    const std::uint32_t dst=flco==3?4209000U:99U;
    constexpr std::uint32_t src=3024109U;
    p[3]=static_cast<std::uint8_t>(dst>>16U);
    p[4]=static_cast<std::uint8_t>(dst>>8U);
    p[5]=static_cast<std::uint8_t>(dst);
    p[6]=static_cast<std::uint8_t>(src>>16U);
    p[7]=static_cast<std::uint8_t>(src>>8U);
    p[8]=static_cast<std::uint8_t>(src);
    for(unsigned i=0;i<9;++i)
        for(unsigned j=0;j<3;++j) p[9+j]^=gf(p[i],kRs[i][j]);
    for(unsigned j=9;j<12;++j) p[j]^=static_cast<std::uint8_t>(type==1?0x96U:0x99U);
    return p;
}
std::uint16_t crc_ccitt(const Data& p) {
    // ETSI Annex B.3.8: CRC-CCITT, initial zero and complement at finish.
    std::uint16_t c=0;
    for(unsigned j=0;j<10;++j) {
        for(unsigned b=0;b<8;++b) {
            const bool carry=(c&0x8000U)!=0;
            c=static_cast<std::uint16_t>(c<<1U);
            if ((p[j]&(0x80U>>b))!=0U) c^=0x1021U;
            if(carry) c^=0x1021U;
        }
    }
    return static_cast<std::uint16_t>(c^0xffffU);
}
Data block_payload(unsigned type) {
    if(type==1||type==2) return full_lc(type, type==2?3:0);
    Data p{};
    for(unsigned i=0;i<10;++i) p[i]=static_cast<std::uint8_t>(i*17U+type*3U);
    const std::uint16_t mask=type==3?0xa5a5U:(type==6?0xccccU:0x3333U);
    const auto c=static_cast<std::uint16_t>(crc_ccitt(p)^mask);
    p[10]=static_cast<std::uint8_t>(c>>8U);
    p[11]=static_cast<std::uint8_t>(c);
    return p;
}
std::array<std::uint8_t, 196> bptc(const Data& p) {
    std::uint8_t m[13][15]{};
    for(unsigned i=0;i<96;++i)
        m[(i+3)/11][(i+3)%11]=static_cast<std::uint8_t>(getbit(p.data(),i));
    for(unsigned r=0;r<9;++r) {
        unsigned parity=0;
        for(unsigned c=0;c<11;++c) if(m[r][c])parity^=kRow[c];
        for(unsigned c=0;c<4;++c)m[r][11+c]=static_cast<std::uint8_t>((parity>>(3-c))&1U);
    }
    for(unsigned col=0;col<15;++col) {
        unsigned parity=0;
        for(unsigned r=0;r<9;++r)if(m[r][col])parity^=kColumn[r];
        for(unsigned r=0;r<4;++r)m[9+r][col]=static_cast<std::uint8_t>((parity>>(3-r))&1U);
    }
    // ETSI Fig. B.1 maps *encoded* index i -> transmitted index 181*i mod 196.
    std::array<std::uint8_t,196> wire{};
    for(unsigned r=0;r<13;++r)for(unsigned c=0;c<15;++c) {
        const unsigned pos=1U+r*15U+c;
        wire[(pos*181U)%196U]=m[r][c];
    }
    return wire; // encoded index zero is reserved and remains zero
}
Burst make_burst(unsigned kind, unsigned type, const Data& data) {
    std::array<std::uint8_t,264> b{};
    const auto payload=bptc(data);
    for(unsigned i=0;i<196;++i) b[i<98?i:i+68]=payload[i];
    const auto st=golay(static_cast<std::uint8_t>((7U<<4U)|type));
    for(unsigned i=0;i<10;++i)b[98+i]=static_cast<std::uint8_t>((st>>(19U-i))&1U);
    for(unsigned i=0;i<10;++i)b[156+i]=static_cast<std::uint8_t>((st>>(9U-i))&1U);
    for(unsigned i=0;i<48;++i)b[108+i]=static_cast<std::uint8_t>((kDataSync[kind]>>(47U-i))&1U);
    Burst out{};
    for(unsigned i=0;i<132;++i)out[i]=static_cast<std::uint8_t>((b[2*i]<<1U)|b[2*i+1]);
    return out;
}
void sync_bits(Burst& data,std::uint64_t word) {
    for(unsigned i=0;i<24;++i)
        data[54+i]=static_cast<std::uint8_t>((word>>(46U-2U*i))&3U);
}
void flip_sync(Burst& data, unsigned i) {
    REQUIRE(i<48);
    data[54+i/2U]^=static_cast<std::uint8_t>((i%2U)?1U:2U);
}
std::array<std::uint8_t,12> cach_prefix(unsigned slot) {
    // independent TACT mapping from ETSI TS 102 361-1 Fig. B.9, not D0 pack_cach.
    std::array<std::uint8_t,24> b{};
    const auto info=static_cast<std::uint8_t>(slot==2?4U:0U);
    const auto d3=(info>>3)&1U,d2=(info>>2)&1U,d1=(info>>1)&1U,d0=info&1U;
    const auto cw=static_cast<std::uint8_t>((info<<3U)|
        ((d3^d2^d1)<<2U)|((d2^d1^d0)<<1U)|(d3^d2^d0));
    constexpr unsigned locations[7]={0,4,8,12,14,18,22};
    for(unsigned i=0;i<7;++i)b[locations[i]]=static_cast<std::uint8_t>((cw>>(6U-i))&1U);
    std::array<std::uint8_t,12> p{};
    for(unsigned i=0;i<12;++i)p[i]=static_cast<std::uint8_t>((b[2*i]<<1U)|b[2*i+1]);
    return p;
}
void collect(const Event& e,void* user) noexcept {
    auto* events=static_cast<std::vector<Event>*>(user);
    events->push_back(e);
}
std::vector<Event> run(const std::uint8_t* data, std::size_t n, std::size_t chunk=1024) {
    Decoder d;
    std::vector<Event> events;
    std::size_t pos=0;
    while(pos<n) {
        const auto take=std::min(chunk,n-pos);
        REQUIRE(d.feed(data+pos,take,collect,&events));
        pos+=take;
    }
    metrics.examined_bits+=2U*n;
    metrics.events+=events.size();
    for(const auto& e:events) {
        if(e.integrity_valid && e.identity!=IdentityKind::none)++metrics.identities;
        else ++metrics.tentative;
    }
    return events;
}
std::vector<Event> run(const Burst& b,std::size_t chunk=1024) {
    return run(b.data(),b.size(),chunk);
}
void require_verified(const Event& e, unsigned kind,unsigned type,unsigned sync_errors) {
    REQUIRE(e.sync==kDataKind[kind]);
    REQUIRE(e.sync_bit_errors==sync_errors);
    REQUIRE(e.colour_code==7U);
    REQUIRE(e.data_type==type);
    REQUIRE(e.golay_valid && e.bptc_valid && e.integrity_valid);
    if (type==1||type==2) {
        REQUIRE(e.evidence==Evidence::identity_valid);
        REQUIRE(e.source_id==3024109U);
        REQUIRE(e.destination_id==(type==1?99U:4209000U));
        REQUIRE(e.identity==(type==1?IdentityKind::group:IdentityKind::private_call));
    } else {
        REQUIRE(e.evidence==Evidence::check_valid);
        REQUIRE(e.identity==IdentityKind::none);
        REQUIRE(e.source_id==0U && e.destination_id==0U);
    }
    const auto expected_slot=kind==2?1U:(kind==3?2U:0U);
    REQUIRE(e.slot==expected_slot);
    REQUIRE(!e.slot_from_cach);
}
void test_one_bit_all_payloads() {
    for(unsigned kind=0;kind<4;++kind)for(unsigned type:{1U,2U,3U,6U,11U}) {
        const auto b=make_burst(kind,type,block_payload(type));
        auto events=run(b);
        REQUIRE(events.size()==1U);
        require_verified(events.front(),kind,type,0);
        for(unsigned position=0;position<48;++position) {
            auto f=b;
            flip_sync(f,position);
            events=run(f);
            ++metrics.one_bit;
            ++metrics.candidates;
            REQUIRE(events.size()==1U);
            require_verified(events.front(),kind,type,1);
        }
    }
    std::printf("PASS one-bit exhaustive 4 DATA sync kinds x 5 ETSI packet types x 48 bits = %llu candidates\n",
                static_cast<unsigned long long>(metrics.one_bit));
}
void test_multibit_exhaustive() {
    for(unsigned kind=0;kind<4;++kind) {
        const auto valid=make_burst(kind,1,full_lc(1));
        // All C(48,2) and C(48,3), not a sample of convenient bit positions.
        for(unsigned a=0;a<48;++a)for(unsigned b=a+1;b<48;++b) {
            auto f=valid;
            flip_sync(f,a);flip_sync(f,b);
            ++metrics.two_bit;
            ++metrics.candidates;
            REQUIRE(run(f).empty());
            for(unsigned c=b+1;c<48;++c) {
                auto g=f;
                flip_sync(g,c);
                ++metrics.three_bit;
                ++metrics.candidates;
                REQUIRE(run(g).empty());
            }
        }
        // Hard cases with 4, 8, 16, 24, 36 or all 48 flipped bits.
        for(unsigned count:{4U,8U,16U,24U,36U,48U}) {
            auto f=valid;
            for(unsigned i=0;i<count;++i)flip_sync(f,i);
            ++metrics.high_bit;
            ++metrics.candidates;
            REQUIRE(run(f).empty());
        }
    }
    std::printf("PASS multi-bit: two=%llu three=%llu >=four=%llu, all rejected\n",
                static_cast<unsigned long long>(metrics.two_bit),
                static_cast<unsigned long long>(metrics.three_bit),
                static_cast<unsigned long long>(metrics.high_bit));
}
void test_integrity_gate() {
    for(unsigned kind=0;kind<4;++kind)for(unsigned type:{1U,2U,3U,6U,11U}) {
        auto bad=block_payload(type);
        // BPTC is fully recomputed, so only the independent RS/CRC checksum is bad.
        bad[7]^=0x20U;
        for(unsigned pos:{0U,13U,47U}) {
            auto f=make_burst(kind,type,bad);
            flip_sync(f,pos);
            ++metrics.candidates;
            ++metrics.rejected_bad_integrity;
            REQUIRE(run(f).empty());
        }
        // Correct RS/CRC, but invalid reserved R(3) in BPTC.
        auto f=make_burst(kind,type,block_payload(type));
        f[0]^=2U;
        flip_sync(f,5U);
        ++metrics.candidates;
        ++metrics.rejected_bad_integrity;
        REQUIRE(run(f).empty());
        // Golay with four flips, RS/BPTC still good: fuzzy sync stays silent.
        auto g=make_burst(kind,type,block_payload(type));
        for(unsigned i=0;i<4;++i) g[49+i/2U]^=static_cast<std::uint8_t>((i%2U)?1U:2U);
        flip_sync(g,9U);
        ++metrics.candidates;
        ++metrics.rejected_bad_integrity;
        REQUIRE(run(g).empty());
    }
    std::printf("PASS verified-only gate: %llu fuzzy candidates with broken RS/CRC, BPTC or Golay rejected\n",
                static_cast<unsigned long long>(metrics.rejected_bad_integrity));
}
void test_voice_and_geometry() {
    unsigned minimum=48;
    unsigned onebit_intersections=0;
    for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j) {
        minimum=std::min(minimum,popcount(kDataSync[i]^kDataSync[j]));
        for(unsigned p=0;p<48;++p) {
            auto corrupted=kDataSync[i]^(1ULL<<p);
            if(popcount(corrupted^kDataSync[j])==1)++onebit_intersections;
        }
    }
    REQUIRE(minimum>=3U && onebit_intersections==0U);
    for(unsigned kind=0;kind<4;++kind) {
        auto b=make_burst(kind,1,full_lc(1));
        sync_bits(b,kVoiceSync[kind]);
        auto events=run(b);
        REQUIRE(events.size()==1U); // exact voice SYNC is diagnostic only
        REQUIRE(events[0].evidence==Evidence::sync_only);
        REQUIRE(!events[0].integrity_valid && events[0].identity==IdentityKind::none);
        for(unsigned bit=0;bit<48;++bit) {
            auto f=b;
            flip_sync(f,bit);
            ++metrics.candidates;
            ++metrics.rejected_voice;
            REQUIRE(run(f).empty()); // fuzzy VOICE never becomes a data identity
        }
        // Data data-type header + exact *other* voice sync stays voice-only.
        for(unsigned j=0;j<4;++j) {
            auto v=b;sync_bits(v,kVoiceSync[j]);
            events=run(v);
            REQUIRE(events.size()==1U);
            REQUIRE(!events[0].integrity_valid && events[0].identity==IdentityKind::none);
        }
    }
    std::printf("PASS voice-only fuzzy=%llu rejected; min DATA sync distance=%u, one-bit ambiguous intersections=%u\n",
                static_cast<unsigned long long>(metrics.rejected_voice),minimum,onebit_intersections);
}
void test_cach_and_streaming() {
    const auto b=make_burst(0,1,full_lc(1));
    const auto zero=std::array<std::uint8_t,12>{};
    std::vector<std::uint8_t> arbitrary(zero.begin(),zero.end());
    arbitrary.insert(arbitrary.end(),b.begin(),b.end());
    auto events=run(arbitrary.data(),arbitrary.size(),1);
    REQUIRE(events.size()==1U);
    REQUIRE(events[0].sync_bit_errors==0U && events[0].integrity_valid);
    REQUIRE(events[0].slot==0U && !events[0].slot_from_cach);
    REQUIRE(events[0].cach_confidence==CachConfidence::tentative);
    REQUIRE(events[0].cach_candidate_slot==1U);
    std::vector<std::uint8_t> wire;
    for(unsigned n=0;n<4;++n) {
        const auto prefix=cach_prefix(n%2?2:1);
        wire.insert(wire.end(),prefix.begin(),prefix.end());
        auto f=make_burst(0,1,full_lc(1));
        flip_sync(f,n%48U); // one-bit fuzzy data sync must still drive valid cadence
        wire.insert(wire.end(),f.begin(),f.end());
    }
    events=run(wire.data(),wire.size(),wire.size());
    REQUIRE(events.size()==4U);
    for(unsigned i=0;i<3;++i) {
        REQUIRE(events[i].slot==0U && !events[i].slot_from_cach);
        REQUIRE(events[i].cach_confidence==CachConfidence::tentative);
    }
    REQUIRE(events[3].slot==2U && events[3].slot_from_cach);
    REQUIRE(events[3].cach_confidence==CachConfidence::cadence_validated);
    for(unsigned chunk:{1U,2U,7U,13U,131U,144U}) {
        auto alt=run(wire.data(),wire.size(),chunk);
        REQUIRE(alt.size()==events.size());
        for(unsigned i=0;i<events.size();++i) {
            REQUIRE(alt[i].burst_start_dibit==events[i].burst_start_dibit);
            REQUIRE(alt[i].slot==events[i].slot);
            REQUIRE(alt[i].sync_bit_errors==events[i].sync_bit_errors);
            REQUIRE(alt[i].identity==events[i].identity);
        }
    }
    auto damaged=wire;
    damaged[2*144+0]^=2U; // corrupt one TACT bit on burst 3 -> no cadence validation
    events=run(damaged.data(),damaged.size(),7);
    REQUIRE(events.size()==4U);
    REQUIRE(events[3].slot==0U && !events[3].slot_from_cach);
    auto gapped=wire;
    gapped.insert(gapped.begin()+144,11,0U); // destroy 144-dibit cadence
    events=run(gapped.data(),gapped.size(),17);
    REQUIRE(events.size()==4U);
    REQUIRE(events[3].slot==0U && !events[3].slot_from_cach);
    // Truncation must never fabricate a final symbol or a burst.
    Decoder d;
    std::vector<Event> out;
    REQUIRE(d.feed(b.data(),b.size()-1,collect,&out));
    REQUIRE(out.empty());
    REQUIRE(d.feed(b.data()+b.size()-1,1,collect,&out));
    REQUIRE(out.size()==1U && out[0].integrity_valid);
    d.reset();out.clear();
    REQUIRE(d.dibits_consumed()==0U);
    REQUIRE(d.feed(b.data()+55,77,collect,&out));
    REQUIRE(out.empty());
    // Reset between alternating CACH bursts destroys the corroboration streak.
    d.reset();out.clear();
    for(unsigned n=0;n<4;++n) {
        if(n==2) d.reset();
        const auto ptr=wire.data()+n*144U;
        REQUIRE(d.feed(ptr,144,collect,&out));
    }
    REQUIRE(out.size()==4U);
    REQUIRE(out.back().slot==0U && !out.back().slot_from_cach);
    // An invalid dibit is an atomic no-op, including within a pending burst.
    d.reset();out.clear();
    std::uint8_t invalid[3]{0,4,2};
    REQUIRE(!d.feed(invalid,3,collect,&out));
    REQUIRE(d.dibits_consumed()==0U && out.empty());
    std::printf("PASS CACH confidence, 144-dibit cadence, damaged TACT, gaps, resets, truncation, chunk invariance\n");
}
void test_randomized_false_positive() {
    constexpr std::size_t n=300000;
    std::mt19937_64 rng(kSeed);
    std::vector<std::uint8_t> random(n);
    for(auto& x:random)x=static_cast<std::uint8_t>(rng()&3U);
    auto events=run(random.data(),random.size(),257);
    REQUIRE(events.empty());
    std::printf("STRESS uniform-random seed=0x%llx dibits=%zu events=%zu identities=0\n",
                static_cast<unsigned long long>(kSeed),n,events.size());
    std::vector<std::uint8_t> am(n);
    // Hard-dibit AM-like proxy: smooth envelope + small deterministic dither.
    // This is NOT actual IQ/AM receiver output and cannot close real-IQ gate.
    constexpr double pi=3.14159265358979323846;
    for(std::size_t i=0;i<n;++i) {
        const double envelope=std::sin(2.0*pi*0.0071*static_cast<double>(i))+
            0.31*std::sin(2.0*pi*0.00023*static_cast<double>(i))+
            0.09*static_cast<double>(static_cast<int>(rng()%1001U)-500)/500.0;
        am[i]=static_cast<std::uint8_t>(envelope>0.6?1:(envelope>0?0:(envelope>-.6?2:3)));
    }
    events=run(am.data(),am.size(),509);
    REQUIRE(events.empty());
    std::printf("STRESS AM-like hard-dibit proxy dibits=%zu events=%zu identities=0\n",n,events.size());
    // Adversarial realistic sync-like random data: many exact/fuzzy sync hits
    // without a valid FEC+integrity payload must NEVER yield a radio identity.
    auto seeded=random;
    constexpr std::size_t stride=997;
    std::size_t injected=0;
    for(std::size_t start=0;start+132<n;start+=stride) {
        const auto kind=static_cast<unsigned>(injected%4U);
        const auto word=kDataSync[kind];
        for(unsigned i=0;i<24;++i)
            seeded[start+54+i]=static_cast<std::uint8_t>((word>>(46U-2U*i))&3U);
        if(injected%2U)seeded[start+54+injected%24U]^=1U;
        ++injected;
    }
    events=run(seeded.data(),seeded.size(),3);
    unsigned exact=0,soft=0,identity=0,diagnostic=0;
    for(const auto& e:events) {
        if(e.sync_bit_errors==0)++exact;
        else ++soft;
        if(e.integrity_valid && e.identity!=IdentityKind::none)++identity;
        else ++diagnostic;
    }
    REQUIRE(identity==0);
    REQUIRE(soft==0); // invalid FEC/CRC fuzzy candidates emit nothing
    REQUIRE(exact>0U);
    REQUIRE(diagnostic==exact);
    metrics.false_identities+=identity;
    std::printf("STRESS injected sync candidates=%zu exact_tentative=%u fuzzy_events=%u verified_identities=%u\n",
                injected,exact,soft,identity);
}
} // namespace
int main() {
    const auto start=std::chrono::steady_clock::now();
#ifndef ORC_TEST_SOURCE_SHA
#define ORC_TEST_SOURCE_SHA "unknown-local"
#endif
#if defined(__clang__)
    std::printf("AUDIT source_sha=%s compiler=Clang %s seed=0x%llx\n",ORC_TEST_SOURCE_SHA,__clang_version__,static_cast<unsigned long long>(kSeed));
#elif defined(__GNUC__)
    std::printf("AUDIT source_sha=%s compiler=GCC %s seed=0x%llx\n",ORC_TEST_SOURCE_SHA,__VERSION__,static_cast<unsigned long long>(kSeed));
#else
    std::printf("AUDIT source_sha=%s compiler=other seed=0x%llx\n",ORC_TEST_SOURCE_SHA,static_cast<unsigned long long>(kSeed));
#endif
    test_one_bit_all_payloads();
    test_multibit_exhaustive();
    test_integrity_gate();
    test_voice_and_geometry();
    test_cach_and_streaming();
    test_randomized_false_positive();
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::printf("RESULT examined_bits=%llu candidates=%llu events=%llu tentative=%llu verified_identities=%llu false_identities=%llu seconds=%.6f\n",
                static_cast<unsigned long long>(metrics.examined_bits),
                static_cast<unsigned long long>(metrics.candidates),
                static_cast<unsigned long long>(metrics.events),
                static_cast<unsigned long long>(metrics.tentative),
                static_cast<unsigned long long>(metrics.identities),
                static_cast<unsigned long long>(metrics.false_identities),seconds);
    std::puts("PASS D1-F independent adversarial audit");
    return 0;
}

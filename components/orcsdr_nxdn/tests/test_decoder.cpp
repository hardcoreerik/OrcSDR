// Independently constructed synthetic NXDN SACCH on-air dibits; not real IQ.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_nxdn/decoder.hpp"
#include "orcsdr_nxdn/frontend.hpp"
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <random>
#include <vector>

using orcsdr::nxdn::Decoder;
using orcsdr::nxdn::Event;
using orcsdr::nxdn::Mode;
using Frame = std::array<std::uint8_t, 192>;
#define TEST(x) do { if(!(x)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); return false; } } while (0)
namespace {

std::uint8_t ref_parity(unsigned v) {
    std::uint8_t p = 0;
    while (v) { p ^= static_cast<std::uint8_t>(v & 1U); v >>= 1U; }
    return p;
}

// Separate CRC oracle: six named register stages, same TS 1-A taps.
std::uint8_t ref_crc6(const std::array<std::uint8_t, 36>& v) {
    std::array<std::uint8_t, 6> s{1,1,1,1,1,1};
    for (std::size_t i=0;i<26;i++) {
        const std::uint8_t a=static_cast<std::uint8_t>(v[i]^s[0]);
        s={static_cast<std::uint8_t>(a^s[1]), s[2],s[3],
           static_cast<std::uint8_t>(a^s[4]),static_cast<std::uint8_t>(a^s[5]),a};
    }
    std::uint8_t r=0;
    for (auto b:s) r=static_cast<std::uint8_t>((r<<1U)|b);
    return r;
}

Frame make_frame(unsigned ran, bool broken_crc=false, bool broken_lich=false) {
    std::array<std::uint8_t, 36> payload{};
    // structure = 2 (second-of-four); RAN is 6-bit, 18-bit non-identity fragment.
    payload[0]=1;payload[1]=0;
    for (unsigned i=0;i<6;i++)payload[i+2]=static_cast<std::uint8_t>((ran>>(5-i))&1U);
    for (unsigned i=0;i<18;i++)payload[8+i]=static_cast<std::uint8_t>(((i*i+3*i+1)>>1U)&1U);
    const auto crc=static_cast<std::uint8_t>(ref_crc6(payload) ^ (broken_crc?1U:0U));
    for(unsigned i=0;i<6;i++)payload[26+i]=static_cast<std::uint8_t>((crc>>(5-i))&1U);
    std::array<std::uint8_t, 72> coded{};
    std::uint8_t state=0;
    for (unsigned i=0;i<36;i++) {
        const auto input=payload[i];
        const auto r=static_cast<std::uint8_t>((state<<1U)|input);
        coded[2*i]=ref_parity(r & 0x19U);
        coded[2*i+1]=ref_parity(r & 0x17U);
        state=static_cast<std::uint8_t>(r & 15U);
    }
    std::array<std::uint8_t, 60> punctured{};
    for (unsigned i=0,j=0;i<72;i++) if (i%6!=5) punctured[j++]=coded[i];
    std::array<std::uint8_t, 60> tx{};
    for (unsigned i=0;i<60;i++) tx[i]=punctured[(i%5)*12+i/5];

    Frame frame{};
    constexpr std::uint32_t sync=0xCDF59U;
    for(unsigned i=0;i<10;i++)frame[i]=static_cast<std::uint8_t>((sync>>(18-2*i))&3U);
    std::uint8_t lich=0xaeU; // RF=2 RDCH, FCT=2 SACCH SF, steal=3, direction=1, parity=0
    if (broken_lich) lich=static_cast<std::uint8_t>(lich^1U);
    std::array<std::uint8_t, 38> raw{};
    for(unsigned i=0;i<8;i++)raw[i]=static_cast<std::uint8_t>((((lich>>(7-i))&1U)<<1U)|1U);
    for(unsigned i=0;i<30;i++)raw[i+8]=static_cast<std::uint8_t>((tx[2*i]<<1U)|tx[2*i+1]);
    std::uint16_t pn=0xe4;
    for(unsigned i=0;i<38;i++) {
        const auto bit=pn&1U;
        const auto fb=((pn>>4U)^pn)&1U;
        pn=static_cast<std::uint16_t>((pn>>1U)|(fb<<8U));
        frame[i+10]=static_cast<std::uint8_t>(raw[i]^(bit?2U:0U));
    }
    return frame;
}

void collect(const Event& e, void* user) noexcept {
    static_cast<std::vector<Event>*>(user)->push_back(e);
}

bool test_mode(Mode mode) {
    const auto a=make_frame(41);
    const auto b=make_frame(12);
    std::vector<std::uint8_t> bits(37,0);
    bits.insert(bits.end(),a.begin(),a.end());
    bits.insert(bits.end(),b.begin(),b.end());
    Decoder decoder(mode);
    std::vector<Event> found;
    TEST(decoder.feed(bits.data(),bits.size(),collect,&found));
    TEST(found.size()==2);
    TEST(found[0].frame_start_dibit==37);
    TEST(found[1].frame_start_dibit==229);
    TEST(found[0].mode==mode && found[1].mode==mode);
    TEST(found[0].ran==41 && found[1].ran==12);
    TEST(found[0].structure==2 && found[1].structure==2);
    TEST(found[0].rf_channel==2 && found[0].functional_channel==2);
    TEST(found[0].steal_option==3 && found[0].direction==1);
    TEST(found[0].sacch_viterbi_distance==0);
    TEST(decoder.counters().sacch_valid==2);
    TEST(decoder.counters().exact_sync_windows==2);

    // Chunking must not alter decoded evidence or frame offsets.
    Decoder chunked(mode);
    std::vector<Event> small;
    for(std::size_t i=0;i<bits.size();) {
        const std::size_t n=(i%13U)+1U;
        const auto size=n<bits.size()-i?n:bits.size()-i;
        TEST(chunked.feed(bits.data()+i,size,collect,&small));
        i+=size;
    }
    TEST(small.size()==2 && small[0].frame_start_dibit==37 && small[1].ran==12);
    TEST(chunked.counters().sacch_valid==decoder.counters().sacch_valid);
    // Strict single-bit sync regression; no fuzzy acceptance.
    auto badsync=a; badsync[0]^=2U;
    Decoder missing(mode);std::vector<Event> negative;
    TEST(missing.feed(badsync.data(),badsync.size(),collect,&negative));
    TEST(negative.empty());
    // A single corrupted SACCH channel bit is corrected by the K=5 trellis.
    auto corrected=a; corrected[19]^=2U;
    Decoder repair(mode); std::vector<Event> repaired;
    TEST(repair.feed(corrected.data(),corrected.size(),collect,&repaired));
    TEST(repaired.size()==1 && repaired[0].ran==41);
    TEST(repaired[0].sacch_viterbi_distance>0);
    // FEC-correct but deliberately incorrect CRC cannot produce RAN.
    auto badcrc=make_frame(41,true);
    Decoder crc_reject(mode);
    TEST(crc_reject.feed(badcrc.data(),badcrc.size(),collect,&negative));
    TEST(negative.empty() && crc_reject.counters().sacch_rejected==1);
    // Invalid LICH parity disallows unverified SACCH identities.
    auto badlich=make_frame(41,false,true);
    Decoder lich_reject(mode);
    TEST(lich_reject.feed(badlich.data(),badlich.size(),collect,&negative));
    TEST(negative.empty() && lich_reject.counters().lich_valid==0);
    // Input validity must be checked before any state changes.
    Decoder invalid(mode);std::array<std::uint8_t,4> wrong{1,2,4,3};
    TEST(!invalid.feed(wrong.data(),wrong.size(),collect,&negative));
    TEST(!invalid.feed(nullptr,1,collect,&negative));
    TEST(invalid.counters().dibits_seen==0);
    TEST(invalid.feed(nullptr,0,collect,&negative));
    TEST(invalid.feed(a.data(),a.size(),nullptr)); // null callback allowed for counters
    TEST(invalid.counters().sacch_valid==1);
    invalid.reset();
    TEST(invalid.counters().dibits_seen==0 && invalid.counters().sacch_valid==0);
    return true;
}

bool noise_test() {
    std::mt19937 rng(0x21A0DD);
    std::vector<std::uint8_t> samples(400000);
    for (auto& x:samples)x=static_cast<std::uint8_t>(rng()&3U);
    Decoder decoder(Mode::nxdn48);
    std::vector<Event> found;
    TEST(decoder.feed(samples.data(),samples.size(),collect,&found));
    TEST(found.empty());
    Decoder invalid(static_cast<Mode>(33));
    TEST(!invalid.feed(samples.data(),10,collect,&found));
    return true;
}
}


struct StreamCtx { Decoder* decoder; std::vector<Event>* events; };
void feed_symbol(std::uint8_t d,void* arg) noexcept {
    auto* ctx=static_cast<StreamCtx*>(arg);
    (void)ctx->decoder->feed(&d,1,collect,ctx->events);
}
bool test_iq(Mode mode) {
    using orcsdr::nxdn::IQSample;
    using orcsdr::nxdn::Frontend;
    constexpr double pi=3.141592653589793;
    std::vector<std::uint8_t> dibits(30,0);
    for(unsigned i=0;i<8;i++) {
        auto f=make_frame(41);
        // Extend PN9 across the remaining 144 information dibits.
        std::uint16_t pn=0xe4;
        for(unsigned k=0;k<182;++k) {
            auto bit=pn&1U;
            auto fb=((pn>>4U)^pn)&1U;
            pn=static_cast<std::uint16_t>((pn>>1U)|(fb<<8U));
            if(k>=38 && bit)f[10+k]^=2U;
        }
        dibits.insert(dibits.end(),f.begin(),f.end());
    }
    const unsigned sps=mode==Mode::nxdn48?20:10;
    std::vector<IQSample> iq(dibits.size()*sps);
    double phase=0;
    for(std::size_t i=0;i<iq.size();++i) {
        std::uint8_t d=dibits[i/sps];
        const double deviation=d==1?1800:(d==0?600:(d==2?-600:-1800));
        phase+=2*pi*(deviation+350)/48000;
        iq[i]={static_cast<float>(std::cos(phase)),static_cast<float>(std::sin(phase))};
    }
    Frontend frontend(mode);Decoder decoder(mode);
    std::vector<Event> events;
    StreamCtx ctx{&decoder,&events};
    for(std::size_t i=0;i<iq.size();) {
        const auto n=std::min<std::size_t>(iq.size()-i,37+i%67);
        TEST(frontend.feed(iq.data()+i,n,feed_symbol,&ctx));
        i+=n;
    }
    std::printf("IQ rate=%u samplecount=%zu symbols=%llu event=%zu offset=%g\n",
                sps==20?2400:4800,iq.size(),
                static_cast<unsigned long long>(frontend.symbols_seen()),
                events.size(),frontend.carrier_offset_hz());
    TEST(events.size()>=2);
    for(const auto& e:events) TEST(e.ran==41);
    return true;
}

int main() {
    if (!test_mode(Mode::nxdn48) || !test_mode(Mode::nxdn96) || !noise_test() || !test_iq(Mode::nxdn48) || !test_iq(Mode::nxdn96)) return 1;
    std::puts("PASS NXDN48/NXDN96 bounded hard-dibit SACCH/RAN synthetic tests");
    return 0;
}

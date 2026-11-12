// D1-CACH-01 independent burst/framing regressions; no RF reception claims.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/d1.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace orcsdr::dmr;
using namespace orcsdr::dmr::d1;
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);return false;}}while(false)
namespace {
using Burst = std::array<std::uint8_t,132>;
using Prefix = std::array<std::uint8_t,12>;
Burst voice(SyncKind kind){
    Burst b{};
    const auto sync=sync_word(kind);
    for(unsigned i=0;i<24;++i)b[54+i]=static_cast<std::uint8_t>((sync>>(46-2*i))&3U);
    return b;
}
Prefix make_cach(unsigned slot){
    CachFields f{};f.tdma_channel=(slot==2);f.payload=0x174a;
    CachWire word{};if(!pack_cach(f,word))std::abort();
    Prefix p{};
    for(unsigned i=0;i<12;++i)p[i]=static_cast<std::uint8_t>((word[i/4]>>(6-2*(i%4)))&3U);
    return p;
}
void add(std::vector<std::uint8_t>& wire,const Prefix& p,const Burst& b){
    wire.insert(wire.end(),p.begin(),p.end());wire.insert(wire.end(),b.begin(),b.end());
}
void collect(const Event&e,void*ctx)noexcept{static_cast<std::vector<Event>*>(ctx)->push_back(e);}
std::vector<Event> run(const std::vector<std::uint8_t>& wire,std::size_t chunk=1000000){
    std::vector<Event> events;Decoder d;
    for(std::size_t i=0;i<wire.size();i+=chunk){
        const auto length=(chunk<wire.size()-i)?chunk:(wire.size()-i);
        if(!d.feed(wire.data()+i,length,collect,&events))std::abort();
    }
    return events;
}
bool not_confirmed(const Event&e){
    return e.slot==0&&!e.slot_from_cach&&!e.cach_present&&
           e.cach_confidence!=CachConfidence::cadence_validated&&
           e.cach_confidence!=CachConfidence::embedded_lc_validated;
}
bool arbitrary_prefixes(){
    const auto b=voice(SyncKind::bs_voice);
    for(unsigned seed=0;seed<512;++seed){
        std::vector<std::uint8_t> wire;
        std::uint32_t rng=0xb7e15162U^seed;
        for(unsigned i=0;i<12;++i){rng=rng*1664525U+1013904223U;wire.push_back(static_cast<std::uint8_t>(rng>>30));}
        if(seed==0)wire.assign(12,0); // Exact TACT=0 formerly falsely confirmed TS1
        wire.insert(wire.end(),b.begin(),b.end());
        const auto ev=run(wire,(seed%17)+1);
        CHECK(ev.size()==1&&not_confirmed(ev[0]));
        CHECK(ev[0].identity==IdentityKind::none&&!ev[0].integrity_valid);
    }
    return true;
}
bool alternating_cadence(){
    std::vector<std::uint8_t> wire{3,1,2,0,2};
    for(unsigned i=0;i<5;++i)add(wire,make_cach(i%2+1),voice(SyncKind::bs_voice));
    const auto baseline=run(wire);
    CHECK(baseline.size()==5);
    for(unsigned i=0;i<3;++i){
        CHECK(not_confirmed(baseline[i]));
        CHECK(baseline[i].cach_tact_decoded);
        CHECK(baseline[i].cach_candidate_slot==i%2+1);
        CHECK(baseline[i].cach_cadence_bursts==i+1);
    }
    for(unsigned i=3;i<5;++i){
        CHECK(baseline[i].slot==i%2+1&&baseline[i].slot_from_cach&&baseline[i].cach_present);
        CHECK(baseline[i].cach_confidence==CachConfidence::cadence_validated);
        CHECK(baseline[i].cach_cadence_bursts==CachCadence::kConfirmationBursts);
        CHECK(baseline[i].evidence==Evidence::sync_only&&baseline[i].identity==IdentityKind::none);
    }
    for(unsigned chunk : {1U,2U,3U,7U,13U,131U,144U}){
        const auto ev=run(wire,chunk);CHECK(ev.size()==baseline.size());
        for(unsigned i=0;i<baseline.size();++i){
            CHECK(ev[i].burst_start_dibit==baseline[i].burst_start_dibit);
            CHECK(ev[i].slot==baseline[i].slot);
            CHECK(ev[i].cach_confidence==baseline[i].cach_confidence);
            CHECK(ev[i].cach_cadence_bursts==baseline[i].cach_cadence_bursts);
        }
    }
    for(unsigned chop=1;chop<132;++chop){
        auto truncated=wire;truncated.resize(truncated.size()-chop);
        const auto ev=run(truncated,1);
        CHECK(ev.size()==4);CHECK(ev[3].slot==2);
    }
    return true;
}
bool corrupted_and_breaks(){
    std::vector<std::uint8_t> wire;
    for(unsigned i=0;i<7;++i){
        auto p=make_cach(i%2+1);
        if(i==2)p[0]^=2U; // flip a TACT bit: D0 "corrects", D1 must reject as cadence evidence
        add(wire,p,voice(SyncKind::bs_voice));
    }
    const auto ev=run(wire,1);CHECK(ev.size()==7);
    for(unsigned i=0;i<6;++i)CHECK(not_confirmed(ev[i]));
    CHECK(ev[2].cach_tact_corrections==1);
    CHECK(ev[2].cach_confidence==CachConfidence::tentative);
    CHECK(ev[6].slot==1&&ev[6].cach_confidence==CachConfidence::cadence_validated);
    wire.clear();
    for(unsigned i=0;i<7;++i){
        add(wire,make_cach(1),voice(SyncKind::bs_voice));
    }
    for(const auto&e:run(wire,7))CHECK(not_confirmed(e));
    wire.clear();
    for(unsigned i=0;i<6;++i){
        if(i==2)wire.push_back(3); // 145, not 144 dibits between bursts
        add(wire,make_cach(i%2+1),voice(SyncKind::bs_voice));
    }
    for(unsigned i=0;i<5;++i)CHECK(not_confirmed(run(wire)[i]));
    return true;
}
bool direct_mode_independent(){
    std::vector<std::uint8_t> wire;
    auto a=voice(SyncKind::direct_ts1_voice),b=voice(SyncKind::direct_ts2_voice);
    wire.insert(wire.end(),a.begin(),a.end());wire.insert(wire.end(),b.begin(),b.end());
    const auto ev=run(wire,3);CHECK(ev.size()==2);
    CHECK(ev[0].slot==1&&ev[1].slot==2);
    for(const auto&e:ev){CHECK(e.cach_confidence==CachConfidence::none);
        CHECK(!e.slot_from_cach&&!e.cach_present&&!e.cach_tact_decoded);
        CHECK(e.evidence==Evidence::sync_only&&e.identity==IdentityKind::none);}
    // A non-BS sync between two BS bursts cannot preserve a BS CACH lock.
    wire.clear();
    add(wire,make_cach(1),voice(SyncKind::bs_voice));
    wire.insert(wire.end(),a.begin(),a.end());
    add(wire,make_cach(2),voice(SyncKind::bs_voice));
    const auto mixed=run(wire,1);
    CHECK(mixed.size()==3&&not_confirmed(mixed[0])&&not_confirmed(mixed[2]));
    return true;
}
}
int main(){
    struct Case{const char*name;bool(*fn)();};
    const Case tests[]={{"arbitrary_prefixes",arbitrary_prefixes},
        {"alternating_cadence_chunks_truncation",alternating_cadence},
        {"corrupted_cach_and_cadence_breaks",corrupted_and_breaks},
        {"direct_mode_slot_independent",direct_mode_independent}};
    for(const auto&t:tests){if(!t.fn()){std::fprintf(stderr,"FAIL %s\n",t.name);return 1;}
        std::printf("PASS %s\n",t.name);}
    return 0;
}

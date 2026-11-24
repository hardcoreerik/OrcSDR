// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr_trunk/trunk.hpp"
namespace orcsdr::dmr::trunk {
namespace {
bool freq(uint32_t f) noexcept {return f>=24000000u&&f<=1766000000u;}
uint32_t bits(const Bytes& b,unsigned pos,unsigned n) noexcept {
    uint32_t x=0;for(unsigned i=0;i<n;++i)x=(x<<1)|((b[(pos+i)/8]>>(7-(pos+i)%8))&1u);return x;
}
bool checked(const Bytes& b,uint16_t mask) noexcept {
    return (uint16_t((uint16_t(b[10])<<8)|b[11])^mask)==crc(b);
}
bool header(const Block& b,Grant& g) noexcept {
    if((b.bytes[0]&0x7f)!=0x31||b.bytes[1]!=0)return false;
    if((b.bytes[0]>>7)!=(b.type==3?1:0))return false;
    if(!checked(b.bytes,b.type==3?0xa5a5:0xaaaa))return false;
    g={};g.epoch=b.epoch;g.control_hz=b.frequency;g.ms=b.ms;g.control_slot=b.slot;g.control_cc=b.colour;
    g.lpcn=uint16_t(bits(b.bytes,16,12));g.slot=uint8_t(1+bits(b.bytes,28,1));
    g.late=bits(b.bytes,29,1);g.emergency=bits(b.bytes,30,1);g.offset=bits(b.bytes,31,1);
    g.target=bits(b.bytes,32,24);g.source=bits(b.bytes,56,24);g.replay=b.replay;
    return g.target!=0&&g.source!=0&&(b.type==3?(g.lpcn>0&&g.lpcn<4095):g.lpcn==4095);
}
bool absolute(const Bytes& b,Grant& g) noexcept {
    if(b[0]!=0xb1||(b[1]&0xf0)||bits(b,16,6)!=0||!checked(b,0))return false;
    auto lpcn=bits(b,22,12),txmhz=bits(b,34,10),txstep=bits(b,44,13);
    auto rxmhz=bits(b,57,10),rxstep=bits(b,67,13);
    if(lpcn==0||lpcn==4095||txmhz<50||txmhz>999||rxmhz<50||rxmhz>999||txstep>=8000||rxstep>=8000)return false;
    g.absolute=true;g.absolute_lpcn=uint16_t(lpcn);g.cc=b[1]&15;
    g.tx_hz=txmhz*1000000u+txstep*125u;g.rx_hz=rxmhz*1000000u+rxstep*125u;
    return freq(g.rx_hz);
}
bool same(const Grant& a,const Grant& b) noexcept {
    return a.target==b.target&&a.source==b.source&&a.rx_hz==b.rx_hz&&a.slot==b.slot&&a.cc==b.cc;
}
uint32_t elapsed(uint32_t now,uint32_t then) noexcept{return now-then;}
}
uint16_t crc(const Bytes& b) noexcept {
    uint16_t c=0;for(unsigned i=0;i<10;i++){c^=uint16_t(b[i])<<8;for(unsigned k=0;k<8;k++)c=uint16_t((c<<1)^((c&0x8000)?0x1021:0));}return c^0xffff;
}
Parse Parser::feed(const Block& b,Grant& out) noexcept {
    if(!b.bs||b.slot<1||b.slot>2||!freq(b.frequency)||b.colour>15){reset();return Parse::ignored;}
    auto& p=p_[b.slot-1];
    auto old=p;p={};
    if(!b.fec||!b.exact)return Parse::ignored;
    Grant g{};
    if(b.type==3){if(!header(b,g))return Parse::rejected;out=g;return Parse::grant;}
    if(b.type==4){if(!header(b,g))return Parse::rejected;p={b,true};return Parse::pending;}
    if(b.type!=5)return Parse::ignored;
    if(!old.valid||old.block.epoch!=b.epoch||old.block.frequency!=b.frequency||
       old.block.colour!=b.colour||old.block.replay!=b.replay||
       b.dibit<=old.block.dibit||b.dibit-old.block.dibit!=288||
       elapsed(b.ms,old.block.ms)>180)return Parse::rejected;
    if(!header(old.block,g)||!absolute(b.bytes,g))return Parse::rejected;
    g.ms=b.ms;out=g;return Parse::grant;
}
MapResult ChannelMap::observe(uint16_t lpcn,uint32_t hz,uint8_t cc) noexcept {
    if(!lpcn||lpcn>=4095||!freq(hz)||cc>15)return MapResult::invalid;
    for(auto& e:m_)if(e.lpcn==lpcn){if(e.conflict||e.hz!=hz||e.cc!=cc){e.conflict=true;return MapResult::conflict;}return MapResult::confirmed;}
    for(auto& e:m_)if(!e.lpcn){e={lpcn,hz,cc,false};return MapResult::inserted;}
    return MapResult::full;
}
const Mapping* ChannelMap::find(uint16_t lpcn)const noexcept {
    if(lpcn)for(const auto& e:m_)if(e.lpcn==lpcn&&!e.conflict)return &e;return nullptr;
}
void Follower::queue(ActionKind k,uint32_t hz,uint8_t slot) noexcept {pending_={k,hz,++token_,slot,replay_};}
bool Follower::start(uint32_t f,uint32_t e,uint32_t now,bool replay) noexcept {
    if(!freq(f)||state_!=State::idle)return false;
    if(control_!=f||replay_!=replay)map_.reset();
    control_=f;epoch_=e;replay_=replay;previous_valid_=false;active_={};started_=last_=now;state_=State::control;pending_={};return true;
}
void Follower::stop() noexcept {state_=State::idle;active_={};previous_valid_=false;queue(ActionKind::stop,0);}
void Follower::go_control(uint32_t now) noexcept {
    previous_=active_;previous_ms_=now;previous_valid_=active_.target!=0;deadline_start_=now;
    if(state_==State::tune_voice||active_.rx_hz!=control_){state_=State::tune_control;queue(ActionKind::tune,control_);}
    else {state_=State::control;active_={};queue(ActionKind::mute,control_);}
}
void Follower::skip(uint32_t now) noexcept {if(state_==State::acquire||state_==State::listening||state_==State::tune_voice)go_control(now);}
void Follower::tick(uint32_t now,bool ready) noexcept {
    if(state_==State::idle||state_==State::failed)return;
    if(!ready){state_=State::failed;queue(ActionKind::stop,0);return;}
    if((state_==State::tune_voice||state_==State::tune_control)&&elapsed(now,deadline_start_)>=cfg_.tune_ms){
        state_=State::failed;queue(ActionKind::stop,0);return;}
    if(state_==State::acquire&&elapsed(now,started_)>=cfg_.acquire_ms)go_control(now);
    else if(state_==State::listening&&(elapsed(now,last_)>=cfg_.hang_ms||elapsed(now,started_)>=cfg_.max_call_ms))go_control(now);
}
void Follower::discontinuity(uint32_t e,uint32_t now) noexcept {
    epoch_=e;
    if(state_==State::acquire||state_==State::listening)go_control(now);
    else if(state_==State::control)pending_={};
    else if(state_==State::tune_voice||state_==State::tune_control){state_=State::failed;queue(ActionKind::stop,0);}
}
bool Follower::grant(const Grant& input,uint32_t now) noexcept {
    if(state_!=State::control||pending_.kind!=ActionKind::none||input.control_hz!=control_||input.epoch!=epoch_||input.replay!=replay_||
       elapsed(now,input.ms)>cfg_.grant_age_ms||input.slot<1||input.slot>2||!input.target||!input.source||input.control_slot<1||input.control_slot>2)return false;
    auto g=input;
    if(g.absolute){
        auto r=map_.observe(g.absolute_lpcn,g.rx_hz,g.cc);
        if(r==MapResult::invalid||r==MapResult::conflict)return false;
    }else {
        auto m=map_.find(g.lpcn);if(!m)return false;
        g.rx_hz=m->hz;g.cc=m->cc;
    }
    if(!freq(g.rx_hz)||(target_filter_&&target_filter_!=g.target))return false;
    if(previous_valid_&&g.late&&same(g,previous_)&&elapsed(now,previous_ms_)<cfg_.duplicate_ms)return false;
    active_=g;started_=last_=deadline_start_=now;
    if(g.rx_hz==control_){state_=State::acquire;queue(ActionKind::select_slot,g.rx_hz,g.slot);}
    else {state_=State::tune_voice;queue(ActionKind::tune,g.rx_hz,g.slot);}
    return true;
}
bool Follower::tune_result(uint32_t token,bool ok,uint32_t f,uint32_t e,uint32_t now) noexcept {
    if(token!=token_||(state_!=State::tune_voice&&state_!=State::tune_control))return false;
    if(!ok||e==epoch_||f!=(state_==State::tune_voice?active_.rx_hz:control_)){
        state_=State::failed;queue(ActionKind::stop,0);return false;}
    epoch_=e;
    if(state_==State::tune_control){state_=State::control;active_={};queue(ActionKind::mute,control_);}
    else {state_=State::acquire;started_=last_=now;queue(ActionKind::select_slot,f,active_.slot);}
    return true;
}
bool Follower::voice(const Voice& v,uint32_t now) noexcept {
    if((state_!=State::acquire&&state_!=State::listening)||!v.validated||v.epoch!=epoch_||v.replay!=replay_||
       v.hz!=active_.rx_hz||v.slot!=active_.slot||v.cc!=active_.cc||v.target!=active_.target||
       v.source!=active_.source||elapsed(now,v.ms)>cfg_.grant_age_ms)return false;
    if(v.ended||!v.clear){go_control(now);return false;}
    last_=now;state_=State::listening;return true;
}
}

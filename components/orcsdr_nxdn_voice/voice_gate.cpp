// SPDX-License-Identifier: AGPL-3.0-or-later
#include "voice_gate.hpp"
#include <algorithm>
namespace orcsdr::nxdn::voice {
namespace {
unsigned parity(unsigned x){x^=x>>4;x^=x>>2;x^=x>>1;return x&1;}
unsigned crc(const uint8_t* b,unsigned n,unsigned width,unsigned poly){unsigned c=(1u<<width)-1;for(unsigned i=0;i<n;i++){auto f=((c>>(width-1))^b[i])&1;c=((c<<1)&((1u<<width)-1))^(f?poly:0);}return c;}
unsigned value(const uint8_t* b,unsigned n){unsigned x=0;while(n--)x=(x<<1)|*b++;return x;}
bool same(const Call&a,const Call&b){return a.source==b.source&&a.destination==b.destination&&a.type==b.type&&a.option==b.option&&a.cipher==b.cipher&&a.key==b.key&&a.privacy==b.privacy;}
Call parse(const uint8_t* b,bool rate96){Call c{};if(b[0]!=1)return c;c.source=uint16_t((b[3]<<8)|b[4]);c.destination=uint16_t((b[5]<<8)|b[6]);c.type=b[2]>>5;c.option=b[2]&31;c.cipher=b[7]>>6;c.key=b[7]&63;
 if(c.cipher||c.key)c.privacy=Privacy::protected_voice;else if(c.option!=(rate96?2:0)||!c.source)c.privacy=Privacy::unsupported;else c.privacy=Privacy::clear;return c;}
}
bool facch(const uint8_t* wire,std::array<uint8_t,10>& payload,uint8_t& distance) noexcept {
 // NXDN TS 1-A FACCH1: inverse 16x9 interleaver, puncture positions 1 mod 4.
 if(!wire)return false;
 std::array<uint8_t,144> ordered{};
 for(unsigned i=0;i<144;i++){if(wire[i]>1)return false;ordered[(i%9)*16+i/9]=wire[i];}
 std::array<int8_t,192> code{};for(unsigned i=0,j=0;i<192;i++)code[i]=i%4==1?-1:int8_t(ordered[j++]);
 std::array<unsigned,16> a{},b{};a.fill(10000);a[0]=0;std::array<std::array<uint8_t,16>,96> trace{};
 for(unsigned t=0;t<96;t++){b.fill(10000);for(unsigned s=0;s<16;s++)for(unsigned bit=0;bit<2;bit++){unsigned reg=(s<<1)|bit,ns=reg&15,c=a[s];c+=code[2*t]>=0&&unsigned(code[2*t])!=parity(reg&0x19);c+=code[2*t+1]>=0&&unsigned(code[2*t+1])!=parity(reg&0x17);if(c<b[ns]){b[ns]=c;trace[t][ns]=uint8_t(s);}}a=b;}
 // Conservative initial acceptance; even a CRC match with severe repairs is held.
 if(a[0]>8)return false;
 std::array<uint8_t,96> d{};unsigned s=0;for(unsigned t=96;t-->0;){d[t]=s&1;s=trace[t][s];}if(s||value(d.data()+92,4)||crc(d.data(),80,12,0x80f)!=value(d.data()+80,12))return false;
 std::array<uint8_t,10> out{};for(unsigned i=0;i<10;i++)out[i]=uint8_t(value(d.data()+8*i,8));payload=out;distance=uint8_t(a[0]);return true;
}
void Gate::invalidate()noexcept{call_={};parts_=held_n_=0;prev_voice96_=false;++call_epoch_;++stats_.resets;}
void Gate::reset()noexcept{invalidate();have_=false;}
Output Gate::consume(const Frame& f)noexcept{
 Output o{};auto finish=[&](){o.call=call_;o.call_epoch=call_epoch_;stats_.accepted+=o.count;return o;};
 // This first qualification covers conventional RDCH and RTCH voice, exact measured FSW only (caller contract).
 if(!f.lich_valid||!f.sacch_valid||(f.rfct!=1&&f.rfct!=2)||f.function>3||f.function==1||f.direction>1||f.steal>3||f.ran>63||f.structure>3){reset();return finish();}
 for(auto b:f.body)if(b>1){reset();return finish();}
 for(auto b:f.fragment)if(b>1){reset();return finish();}
 if(!have_||f.epoch!=epoch_||f.rate96!=rate96_||f.rfct!=rfct_||f.ran!=ran_||f.direction!=direction_||f.start<=prev_||f.start-prev_!=192){reset();}
 have_=true;epoch_=f.epoch;prev_=f.start;rate96_=f.rate96;rfct_=f.rfct;ran_=f.ran;direction_=f.direction;
 // Initial scope: four VCH or no VCH. Mixed FACCH stealing is withheld until separately qualified.
 if(f.steal==1||f.steal==2){invalidate();return finish();}
 if(f.steal==0){
   std::array<uint8_t,10> a{},b{};uint8_t ea=0,eb=0;bool av=facch(f.body.data(),a,ea),bv=facch(f.body.data()+144,b,eb);
   stats_.facch_ok+=unsigned(av)+unsigned(bv);stats_.facch_bad+=unsigned(!av)+unsigned(!bv);
   // Require two matching CRC-valid messages for this initial, conservative gate.
   if(!av||!bv||a!=b){invalidate();return finish();}
   auto c=parse(a.data(),f.rate96);
   if(c.privacy!=Privacy::clear){invalidate();call_=c;return finish();}
   // In 9600 EHR, bracket each voice-bearing RF frame with matching, clear
   // double-FACCH VCALLs. No PCM for the intervening signaling interval.
   if(f.rate96&&prev_voice96_&&held_n_==4&&same(c,call_)) {
       o.frames=held_;o.count=4;o.first_dibit=held_start_;
   }
   if(!same(c,call_)){invalidate();call_=c;}
   prev_voice96_=false;parts_=held_n_=0;return finish();
 }
 if(f.function!=2){invalidate();return finish();}
 std::array<Wire,4> w{};
 for(unsigned j=0;j<4;j++)for(unsigned i=0;i<9;i++)w[j][i]=uint8_t(value(f.body.data()+j*72+i*8,8));
 if(f.rate96){
   if(prev_voice96_){invalidate();return finish();}
   prev_voice96_=true;
   if(call_.privacy==Privacy::clear){held_start_=f.start;held_n_=4;std::copy(w.begin(),w.end(),held_.begin());stats_.held+=4;}
   return finish();
 }
 // Strict 4800 EHR policy: buffer a complete CRC-valid SACCH superframe.
 // Never release the first three blocks of a newly encrypted VCALL just
 // because the preceding VCALL was clear. Unknown L3 stays muted.
 if(f.structure==3){parts_=held_n_=0;held_start_=f.start;}
 if(f.structure!=3-parts_||held_n_>12){invalidate();return finish();}
 for(unsigned i=0;i<18;i++)l3_[parts_*18+i]=f.fragment[i];
 ++parts_;for(auto& x:w)held_[held_n_++]=x;stats_.held+=4;
 if(parts_!=4)return finish();
 std::array<uint8_t,9> msg{};
 for(unsigned i=0;i<9;i++)msg[i]=uint8_t(value(l3_.data()+8*i,8));
 auto c=parse(msg.data(),false);
 if(c.privacy!=Privacy::clear){invalidate();call_=c;return finish();}
 if(!same(c,call_)){call_=c;++call_epoch_;++stats_.late_calls;}
 o.frames=held_;o.count=held_n_;o.first_dibit=held_start_;parts_=held_n_=0;
 return finish();
}
}

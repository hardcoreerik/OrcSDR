// SPDX-License-Identifier: AGPL-3.0-or-later
#include "voice_gate.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
using namespace orcsdr::nxdn::voice;
#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": "<<#x<<"\n";std::exit(1);}}while(0)
using Msg=std::array<uint8_t,10>;
Msg msg(bool r,uint8_t cipher=0,uint8_t type=1){return {type,0,uint8_t(0x20|(r?2:0)),0x22,0x6e,0,0,uint8_t(cipher<<6),0,0};}
std::array<uint8_t,144> encode(const Msg& m){
 std::array<uint8_t,96>d{};for(unsigned i=0;i<80;i++)d[i]=(m[i/8]>>(7-i%8))&1;
 unsigned c=0xfff;for(unsigned i=0;i<80;i++){bool bit=((c&0x800)!=0)!=bool(d[i]);c=(c<<1)&0xfff;if(bit)c^=0x80f;}
 for(unsigned i=0;i<12;i++)d[80+i]=(c>>(11-i))&1;
 unsigned r=0;std::array<uint8_t,144> order{},out{};unsigned n=0;
 for(unsigned i=0;i<d.size();i++){r=((r<<1)|d[i])&31;auto g1=((r>>4)^(r>>3)^r)&1;auto g2=((r>>4)^(r>>2)^(r>>1)^r)&1;order[n++]=g1;if(i%2)order[n++]=g2;}
 CHECK(n==144);for(unsigned i=0;i<144;i++)out[i]=order[(i%9)*16+i/9];return out;
}
Frame base(bool r,uint64_t pos){Frame f{};f.start=pos;f.epoch=7;f.rate96=r;f.lich_valid=f.sacch_valid=true;f.rfct=2;f.function=2;f.steal=3;f.ran=7;for(unsigned i=0;i<288;i++)f.body[i]=uint8_t((i%7)==0);return f;}
Frame control(uint64_t p,Msg m){auto f=base(true,p);f.steal=0;auto a=encode(m);std::copy(a.begin(),a.end(),f.body.begin());std::copy(a.begin(),a.end(),f.body.begin()+144);return f;}
Frame part(unsigned i,uint64_t p,Msg m){auto f=base(false,p);f.structure=3-i;for(unsigned j=0;j<18;j++){auto b=i*18+j;f.fragment[j]=(m[b/8]>>(7-b%8))&1;}return f;}
int main(){
 auto original=msg(true);auto w=encode(original);std::array<uint8_t,10> m{};uint8_t d=255;CHECK(facch(w.data(),m,d)&&d==0&&m==original);
 for(unsigned i=0;i<144;i++){auto z=w;z[i]^=1;CHECK(facch(z.data(),m,d)&&d==1&&m==original);}
 auto z=w;z[0]=2;CHECK(!facch(z.data(),m,d));
 Gate g;for(unsigned i=0;i<3;i++)CHECK(g.consume(part(i,i*192,msg(false))).count==0);auto out=g.consume(part(3,576,msg(false)));CHECK(out.count==16&&out.first_dibit==0&&out.call.source==8814&&out.call.privacy==Privacy::clear);
 for(unsigned i=0;i<4;i++){CHECK(g.consume(part(i,(i+4)*192,msg(false,1))).count==0);}
 CHECK(g.call().privacy==Privacy::protected_voice);
 for(unsigned i=0;i<4;i++){CHECK(g.consume(part(i,(i+8)*192,msg(false,0,3))).count==0);}
 CHECK(g.call().privacy!=Privacy::clear);
 g.reset();CHECK(g.consume(part(0,0,msg(false))).count==0);CHECK(g.consume(part(1,192,msg(false))).count==0);auto bad=part(2,384,msg(false));bad.sacch_valid=false;CHECK(g.consume(bad).count==0);CHECK(g.consume(part(3,576,msg(false))).count==0);
 g.reset();CHECK(g.consume(control(0,original)).count==0);CHECK(g.consume(base(true,192)).count==0);out=g.consume(control(384,original));CHECK(out.count==4&&out.first_dibit==192&&out.call.privacy==Privacy::clear);
 CHECK(g.consume(base(true,576)).count==0);CHECK(g.consume(control(768,msg(true,1))).count==0);CHECK(g.call().privacy==Privacy::protected_voice);
 CHECK(g.consume(base(true,960)).count==0);CHECK(g.consume(control(1152,original)).count==0);CHECK(g.consume(base(true,1344)).count==0);auto f=control(1536,original);f.epoch=8;CHECK(g.consume(f).count==0);
 CHECK(g.consume(base(true,1728)).count==0); // mismatched epoch
 g.reset();CHECK(g.consume(control(0,original)).count==0);CHECK(g.consume(base(true,192)).count==0);f=control(384,original);f.body[144]=2;CHECK(g.consume(f).count==0);
 g.reset();auto full=original;full[2]=0x23;CHECK(g.consume(control(0,full)).count==0);CHECK(g.call().privacy==Privacy::unsupported);CHECK(g.consume(base(true,192)).count==0);CHECK(g.consume(control(384,full)).count==0);
 g.reset();auto c=base(true,0);c.rfct=0;CHECK(g.consume(c).count==0);CHECK(g.call().privacy==Privacy::unknown);
 std::cout<<"PASS: 144 FACCH single-bit corruptions, independent encoder, ordered 48 EHR, bracketed 96 EHR, protected/unknown/EFR/epoch/invalid/control gating\n";
}

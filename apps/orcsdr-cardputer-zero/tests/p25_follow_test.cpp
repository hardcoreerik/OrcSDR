#include "p25_follow.hpp"
#include <cassert>
#include <iostream>
using namespace orc::zero;
int main(){
 constexpr uint32_t control=851000000,voice=852000000;
 P25Follow f;orcsdr::p25core::Snapshot s;
 assert(f.update(control,true,false,s,100)==control);
 auto grant=[&](uint32_t t){s={};s.current_grant.valid=true;s.current_grant.talkgroup=42;s.current_grant.frequency_hz=voice;s.current_grant.seen_ms=t;};
 grant(200);assert(f.update(control,true,true,s,200)==voice&&f.voice&&f.held==42);
 s={};assert(f.update(control,true,true,s,3199)==voice);
 assert(f.update(control,true,true,s,3200)==control&&!f.voice);
 grant(3300);s.current_grant.talkgroup=43;assert(f.update(control,true,true,s,3300)==control);
 s.current_grant.talkgroup=42;assert(f.update(control,true,true,s,3300)==voice);
 s={};s.last_voice_ms=3500;assert(f.update(control,true,true,s,5299)==voice);
 assert(f.update(control,true,true,s,5300)==control);
 grant(5400);assert(f.update(control,true,false,s,5400)==voice&&f.held==0);
 s={};s.voice_encryption.valid=true;s.voice_encryption.encrypted=true;
 assert(f.update(control,true,false,s,5500)==control);
 grant(5600);assert(f.update(control,true,false,s,5600)==control);
 grant(35600);assert(f.update(control,true,false,s,35600)==voice);
 assert(f.update(control,false,false,s,35700)==control&&!f.voice);
 grant(36000);s.current_grant.tdma=true;assert(f.update(control,true,false,s,36000)==control);
 s.current_grant.tdma=false;s.current_grant.encrypted=true;assert(f.update(control,true,false,s,36000)==control);
 s.current_grant.encrypted=false;assert(f.update(control,true,false,s,38000)==control);
 grant(39000);assert(f.update(control,true,false,s,39000)==voice);
 assert(f.update(control+12500,true,false,s,39100)==control+12500&&!f.voice);
 // Millisecond wrap uses unsigned elapsed arithmetic.
 P25Follow wrap;s={};wrap.update(control,true,false,s,0xfffff000u);
 grant(0xfffffff0u);assert(wrap.update(control,true,false,s,0xfffffff0u)==voice);
 s={};assert(wrap.update(control,true,false,s,0x00000ba8u)==control);
 std::cout<<"P25 follow grants, freshness, acquire/hang, hold, encryption skip, disable and wrap PASS\n";
}

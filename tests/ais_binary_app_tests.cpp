// Synthetic, specification-derived DAC/FI fixtures (not recorded RF).
#include "ais_binary_app.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace app=orcsdr::ais::app;
namespace {
struct Payload {
  uint8_t data[app::kMaxBytes]{};
  void put(size_t offset,unsigned width,uint32_t value) {
    assert(offset+width<=app::kMaxBits&&width<=32);
    for(unsigned i=0;i<width;++i) {
      const size_t pos=offset+i;
      const unsigned bit=(value>>(width-1-i))&1u;
      if(bit)data[pos/8]|=static_cast<uint8_t>(1u<<(7-pos%8));
      else data[pos/8]&=static_cast<uint8_t>(~(1u<<(7-pos%8)));
    }
  }
  void header(uint8_t type,uint32_t source) {
    put(0,6,type);put(8,30,source);
  }
  void id(size_t at,uint16_t dac,uint8_t fi){
    put(at,10,dac);put(at+10,6,fi);
  }
};
void descriptors() {
  assert(app::descriptor_count()==3);
  for(size_t i=0;i<app::descriptor_count();++i)assert(app::descriptor_at(i));
  assert(!app::descriptor_at(app::descriptor_count()));
  assert(app::find(6,1,16)->kind==app::Kind::imo_persons);
  assert(app::find(6,200,55)->kind==app::Kind::inland_persons);
  assert(app::find(8,200,10)->kind==app::Kind::inland_vessel);
  assert(!app::find(8,200,55)); // message type is part of the registry key.
  assert(!app::find(6,1,40));  // do not silently decode unrelated legacy FI.
}
void imo_persons() {
  Payload f;f.header(6,123456789);f.put(40,30,987654321);f.id(72,1,16);
  f.put(88,13,347);
  app::Result r{};
  assert(app::decode(f.data,136,&r)==app::Status::decoded);
  assert(r.kind==app::Kind::imo_persons&&r.persons_known&&r.persons==347);
  assert(r.addressed&&r.has_destination&&r.destination_id==987654321);
  assert(r.has_application_id&&r.dac==1&&r.fi==16);
  assert(r.app_bits==48); // 13 persons + 35 spare.
  f.put(88,13,0);
  assert(app::decode(f.data,136,&r)==app::Status::decoded&&!r.persons_known);
  f.put(88,13,8191);
  assert(app::decode(f.data,136,&r)==app::Status::decoded&&r.persons_at_least_8191);
  assert(app::decode(f.data,96,&r)==app::Status::truncated);
}
void inland_persons() {
  Payload f;f.header(6,244000001);f.put(40,30,244000002);f.id(72,200,55);
  f.put(88,8,12);f.put(96,13,254);f.put(109,8,6);
  app::Result r{};
  assert(app::decode(f.data,168,&r)==app::Status::decoded);
  assert(r.kind==app::Kind::inland_persons&&r.crew==12&&r.passengers==254&&r.personnel==6);
  assert(r.crew_known&&r.passengers_known&&r.personnel_known);
  f.put(88,8,255);f.put(96,13,8191);f.put(109,8,255);
  assert(app::decode(f.data,168,&r)==app::Status::decoded);
  assert(!r.crew_known&&!r.passengers_known&&!r.personnel_known);
  assert(app::decode(f.data,112,&r)==app::Status::truncated);
}
void inland_static() {
  Payload f;f.header(8,244000009);f.id(40,200,10);
  const char eni[]="12345678";
  for(unsigned i=0;i<8;++i)f.put(56+i*6,6,static_cast<unsigned>(eni[i]));
  f.put(104,13,1500);f.put(117,10,98);f.put(127,14,8170);
  f.put(141,3,2);f.put(144,11,345);f.put(155,2,1);f.put(157,3,5);
  app::Result r{};
  assert(app::decode(f.data,168,&r)==app::Status::decoded);
  assert(r.kind==app::Kind::inland_vessel);
  assert(std::strcmp(r.eni,"12345678")==0);
  assert(r.length_dm==1500&&r.beam_dm==98&&r.ship_type==8170);
  assert(r.draught_cm==345&&r.hazardous_cargo==2&&r.loaded==1&&r.quality_flags==5);
  assert(r.length_known&&r.beam_known&&r.draught_known);
  f.put(104,13,0);f.put(117,10,0);f.put(144,11,0);
  assert(app::decode(f.data,168,&r)==app::Status::decoded);
  assert(!r.length_known&&!r.beam_known&&!r.draught_known);
  assert(app::decode(f.data,154,&r)==app::Status::truncated);
}
void opaque_and_unstructured() {
  Payload f;f.header(8,273430180);f.id(40,1,29);
  f.put(56,20,0xabcde);
  app::Result r{};
  assert(app::decode(f.data,80,&r)==app::Status::preserved_unknown);
  assert(r.dac==1&&r.fi==29&&r.app_bits==24);
  assert(r.app_raw[0]==0xab&&r.app_raw[1]==0xcd&&r.app_raw[2]==0xe0);
  assert(r.kind==app::Kind::unknown);
  f.header(25,432000002);
  f.put(38,1,0);f.put(39,1,0);f.put(40,19,0x7aaab);
  assert(app::decode(f.data,59,&r)==app::Status::unstructured);
  assert(!r.has_application_id&&r.app_bits==19);
  f.header(26,273000002);f.put(38,1,1);f.put(39,1,1);
  f.put(40,30,333444555);f.id(70,975,63);f.put(86,17,0x18a3f);
  f.put(103,20,0xfffff); // comm state excluded
  assert(app::decode(f.data,123,&r)==app::Status::preserved_unknown);
  assert(r.destination_id==333444555&&r.dac==975&&r.fi==63);
  assert(r.app_bits==17&&r.app_raw[0]==0xc5&&r.app_raw[1]==0x1f);
  assert(app::decode(f.data,85,&r)==app::Status::truncated);
}
void guards() {
  Payload f;f.header(1,123456789);app::Result r{};
  assert(app::decode(f.data,168,&r)==app::Status::not_binary);
  assert(app::decode(nullptr,168,&r)==app::Status::invalid_input);
  assert(app::decode(f.data,168,nullptr)==app::Status::invalid_input);
  assert(app::decode(f.data,2000,&r)==app::Status::invalid_input);
  f.header(6,1);assert(app::decode(f.data,72,&r)==app::Status::truncated);
}
}
int main() {
  descriptors();imo_persons();inland_persons();inland_static();opaque_and_unstructured();guards();
  std::puts("AIS IALA DAC/FI binary application tests passed (synthetic specification fixtures)");
}

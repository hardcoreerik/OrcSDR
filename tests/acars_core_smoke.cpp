// SPDX-License-Identifier: AGPL-3.0-only
// Minimal standalone CI gate. The larger offline audit is in acars_standard_audit.cpp.
#include "acars_core.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace orcsdr::acars;
namespace {
struct Sink {
  unsigned count=0; Message last{};
  static void receive(void* p, const Message& m){
    auto* self=static_cast<Sink*>(p); ++self->count; self->last=m;
  }
};
std::vector<uint8_t> frame(){
  std::vector<uint8_t> b;
  auto put=[&](uint8_t c){b.push_back(odd_parity_byte(c));};
  put('2'); for(char c:"N123456")if(c)put(uint8_t(c));
  put(0x15); put('Q');put('0');put('1');put(0x02);
  for(char c:"1234UAL111TEST MESSAGE")if(c)put(uint8_t(c));
  put(0x03);
  uint16_t crc=crc16_kermit(b.data(),b.size());
  b.push_back(uint8_t(crc));b.push_back(uint8_t(crc>>8));b.push_back(0x7f);
  return b;
}
void feed(FrameDecoder& f, const std::vector<uint8_t>& b, bool invert=false, bool sync_error=false){
  for(int i=0;i<20*8;++i)f.push_bit(!invert);
  unsigned pos=0;
  auto byte=[&](uint8_t v){for(int bit=0;bit<8;++bit,++pos){
    bool x=((v>>bit)&1)!=0;
    if(sync_error && pos==16+3)x=!x; // one damaged SYN bit
    f.push_bit(x^invert);
  }};
  byte(odd_parity_byte('+'));byte(odd_parity_byte('*'));
  byte(0x16);byte(0x16);byte(0x01);
  for(auto b0:b)byte(b0);
}
}
int main(){
  const uint8_t k7[]={0xcb,0x37,0x3e,0x6b};
  assert(crc16_kermit(k7,4)==0);
  const auto valid=frame();
  assert(crc16_kermit(valid.data(),valid.size()-1)==0);
  for(int invert=0;invert<2;++invert){
    Sink sink;FrameDecoder dec;dec.set_frequency(131550000);dec.set_callback(Sink::receive,&sink);
    feed(dec,valid,invert!=0);
    assert(sink.count==1);
    assert(sink.last.frequency_hz==131550000);
    assert(std::strcmp(sink.last.tail,"N123456")==0);
    assert(std::strcmp(sink.last.flight,"UAL111")==0);
    assert(sink.last.text_length>10);
    assert(!sink.last.corrected && sink.last.end_marker==3);
  }
  {
    auto damaged=valid;damaged[2]^=4;
    Sink sink;FrameDecoder dec;dec.set_callback(Sink::receive,&sink);
    feed(dec,damaged);
    assert(sink.count==1 && sink.last.corrected && sink.last.corrected_bits==1);
  }
  {
    Sink sink;FrameDecoder dec;dec.set_callback(Sink::receive,&sink);
    feed(dec,valid,false,true);
    assert(sink.count==1);
  }
  std::puts("ACARS smoke/CRC/FEC/sync passed");
}

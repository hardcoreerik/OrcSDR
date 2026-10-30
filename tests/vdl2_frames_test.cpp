// SPDX-License-Identifier: AGPL-3.0-only
// Synthetic test vectors ONLY: independently captured IQ/RS vectors are a separate gate.
#include "vdl2_frames.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>
using namespace orcsdr::vdl2;
namespace {
struct Output { unsigned n = 0, acars = 0, raw = 0; char text[32]{}; };
void receive(void* ctx, const FrameView& f) {
  auto& out = *static_cast<Output*>(ctx);
  ++out.n;
  assert(f.raw_size >= 11u);
  if (f.is_acars) {
    ++out.acars;
    assert(f.acars_message != nullptr);
    const auto& m = *f.acars_message;
    assert(m.mode == '2' && m.label[0] == 'H' && m.label[1] == '1');
    assert(std::strncmp(m.tail, ".N123AB", 7) == 0);
    assert(m.text_length == 14);
    assert(std::strncmp(m.flight, "AS0536", 6) == 0);
    std::memcpy(out.text, m.text, m.text_length);
  } else ++out.raw;
}
std::vector<uint8_t> message() {
  std::vector<uint8_t> p{'2','.','N','1','2','3','A','B',0x15,'H','1','2',0x02};
  for (const char* s = "F63AAS0536DATA"; *s; ++s) p.push_back(uint8_t(*s));
  p.push_back(0x03);
  uint16_t crc = acars_crc(p.data(),p.size());
  p.push_back(uint8_t(crc & 0xff));p.push_back(uint8_t(crc >> 8));
  p.push_back(0x7f);
  assert(acars_crc(p.data(),p.size()-1u)==0);
  return p;
}
std::vector<uint8_t> avlc(bool acars) {
  std::vector<uint8_t> p{0x21,0x43,0x65,0x87,0x11,0x23,0x45,0x67,0x00};
  if (acars) {
    p.insert(p.end(), {0xff,0xff,0x01});
    const auto m=message();p.insert(p.end(),m.begin(),m.end());
  } else { p.push_back(0x11);p.push_back(0x22);p.push_back(0x33); }
  const uint16_t fcs=uint16_t(~avlc_crc(p.data(),p.size()));
  p.push_back(uint8_t(fcs));p.push_back(uint8_t(fcs>>8));
  assert(avlc_crc(p.data(),p.size())==0xf0b8u);
  return p;
}
void emit_hdlc(FrameDecoder& d,const std::vector<uint8_t>& p) {
  auto flag=[&](){for(int i=0;i<8;++i)d.push_bit(((0x7e>>i)&1)!=0);};
  flag();unsigned ones=0;
  for(uint8_t b:p)for(int bit=0;bit<8;++bit){
    const bool value=((b>>bit)&1u)!=0;
    d.push_bit(value);
    ones=value?ones+1u:0u;
    if(ones==5){d.push_bit(false);ones=0;}
  }
  flag();
}
}
int main() {
  FrameDecoder d; Output out;
  d.set_callback(receive,&out);d.set_frequency(136975000);
  auto good=avlc(true); emit_hdlc(d,good);
  assert(out.n==1 && out.acars==1 && out.raw==0);
  assert(std::strcmp(out.text,"F63AAS0536DATA")==0);
  assert(d.stats().frames_crc_ok==1);
  auto other=avlc(false);emit_hdlc(d,other);
  assert(out.n==2 && out.raw==1);
  // Outer integrity gate must reject before any interpreted records are created.
  auto corrupted=good;corrupted[10]^=0x01;emit_hdlc(d,corrupted);
  assert(out.n==2 && d.stats().frames_crc_bad==1);
  // Inner CRC failure cannot be silently promoted to ACARS; retain raw hex instead.
  auto inner=good;inner[15]^=0x02;
  uint16_t fcs=uint16_t(~avlc_crc(inner.data(),inner.size()-2));
  inner[inner.size()-2]=uint8_t(fcs);inner[inner.size()-1]=uint8_t(fcs>>8);
  emit_hdlc(d,inner);
  assert(out.n==3 && out.raw==2 && d.stats().acars_bad==1);
  // No allocation, bounded stream: discard a frame exceeding capacity.
  d.reset();for(int bit=0;bit<8;++bit)d.push_bit(((0x7e>>bit)&1)!=0);
  for(size_t i=0;i<(kMaxStuffedBytes+10)*8;i++) d.push_bit(i%2!=0);
  assert(d.stats().overflow > 0);
  std::puts("vdl2_frames_test passed (synthetic framing / CRC / ACARS)");
}

// SPDX-License-Identifier: AGPL-3.0-only
// Synthetic end-to-end VDL2 transmit model is intentionally test-only.
#include "vdl2_receiver.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace orcsdr::vdl2;
namespace {
constexpr double kPi=3.14159265358979323846;
constexpr uint8_t kUw[]={0,3,2,4,0,1,6,4,1,7,2,5,6,5,7,3};
constexpr uint8_t kForward[]={0,7,3,4,1,6,2,5};
constexpr uint8_t kH[5][20]={{0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1},
 {0,0,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1},
 {1,1,0,0,0,1,1,1,0,0,1,1,0,0,0,0,1,1,1,1},
 {1,1,0,1,1,0,1,1,0,1,0,1,0,0,1,1,0,0,1,1},
 {0,1,1,0,1,0,0,1,1,1,1,0,0,1,0,1,0,1}};
void push_bytes(std::vector<uint8_t>& bits,const std::vector<uint8_t>& bytes){
  unsigned ones=0;
  for(uint8_t b:bytes)for(unsigned i=0;i<8;++i){
    const uint8_t bit=(b>>i)&1u;bits.push_back(bit);
    ones=bit?ones+1u:0;
    if(ones==5){bits.push_back(0);ones=0;}
  }
}
std::vector<uint8_t> make_bits(bool is_acars=false) {
  std::vector<uint8_t> frame{0x21,0x43,0x65,0x87,0x11,0x23,0x45,0x67,0x01,0x36,0x75,0xFF};
  if(is_acars){
    frame.resize(9);frame[8]=0x00;
    frame.insert(frame.end(),{0xff,0xff,0x01});
    std::vector<uint8_t> inner{'2','.','N','1','2','3','A','B',0x15,'H','1','2',0x02};
    const char* txt="F63AAS0536DATA";
    while(*txt)inner.push_back(uint8_t(*txt++));
    inner.push_back(0x03);
    const auto bcs=acars_crc(inner.data(),inner.size());
    inner.push_back(uint8_t(bcs));inner.push_back(uint8_t(bcs>>8));inner.push_back(0x7f);
    assert(acars_crc(inner.data(),inner.size()-1)==0);
    frame.insert(frame.end(),inner.begin(),inner.end());
  }
  const uint16_t fcs=uint16_t(~avlc_crc(frame.data(),frame.size()));
  frame.push_back(uint8_t(fcs));frame.push_back(uint8_t(fcs>>8));
  assert(avlc_crc(frame.data(),frame.size())==0xf0b8);
  std::vector<uint8_t> result;
  auto flag=[&]{for(unsigned i=0;i<8;++i)result.push_back((0x7e>>i)&1u);};
  flag();push_bytes(result,frame);flag();
  return result;
}
struct Seen{int frames=0,other=0,acars=0;};
void on_frame(void* ctx,const FrameView& f){
 auto* s=static_cast<Seen*>(ctx);++s->frames;
 if(!f.is_acars)++s->other;
 else {++s->acars; assert(f.acars_message && f.acars_message->text_length==14); assert(f.acars_message->label[0]=='H');}
 assert(f.raw_size>=11);
}
std::vector<ComplexSample> modulated(const std::vector<uint8_t>& bits,uint32_t rate,float cfo){
  const double sps=double(rate)/10500.;
  std::vector<uint8_t> un={0,0,0};
  for(unsigned j=0;j<17;++j)un.push_back(uint8_t((bits.size()>>j)&1u));
  for(unsigned i=0;i<5;++i){unsigned v=0;for(unsigned j=0;j<20;++j)v^=unsigned(un[j]&kH[i][j]);un.push_back(uint8_t(v));}
  std::array<uint8_t,(kMaxVdl2TransmittedBits+7)/8> packed{};
  for(size_t j=0;j<bits.size();++j)if(bits[j])packed[j/8]|=uint8_t(1u<<(j%8));
  RsCodec encoder;
  std::array<uint8_t,(kMaxVdl2TransmittedBits+7)/8> interleaved{};
  size_t txbits=0;
  assert(encoder.interleave(packed.data(),bits.size(),interleaved.data(),interleaved.size()*8,txbits));
  for(size_t j=0;j<txbits;++j)un.push_back((interleaved[j/8]>>(j%8))&1u);
  Descrambler15 s;
  for(auto& b:un)b=s.apply_bit(b);
  while(un.size()%3)un.push_back(0);
  std::vector<uint8_t> deltas(5,0);
  for(uint8_t d:kUw)deltas.push_back(d);
  for(size_t j=0;j<un.size();j+=3)deltas.push_back(kForward[un[j]|(un[j+1]<<1)|(un[j+2]<<2)]);
  std::vector<double> phases;
  double phase=0;
  for(auto d:deltas){phase+=d*kPi/4.;phases.push_back(phase);}
  const size_t n=static_cast<size_t>(std::ceil(double(phases.size())*sps));
  std::vector<ComplexSample> out(n+1000);
  for(size_t j=0;j<n;++j){
    const size_t symbol=std::min(size_t(double(j)/sps),phases.size()-1);
    const double theta=phases[symbol]+(2*kPi*cfo*double(j)/rate);
    out[j+450]={float(std::cos(theta)*0.8),float(std::sin(theta)*0.8)};
  }
  return out;
}
}
int main(){
  const auto payload=make_bits();
  for(const uint32_t rate:{50000u,105000u})for(const float skew:{0.f,20.f}){
    Receiver rx(rate);Seen seen{};
    rx.set_callback(on_frame,&seen);
    const auto samples=modulated(payload,rate,skew);
    for(const auto& s:samples)rx.process_sample(s.i,s.q);
    std::fprintf(stderr,"rate=%u cfo=%g frames=%d uwCand=%u lock=%u hdr=%u/%u fec=%u/%u flags=%u good=%u bad=%u\n",
      rate,skew,seen.frames,rx.stats().uw_candidates,rx.stats().uw_locked,
      rx.stats().header_ok,rx.stats().header_fail,rx.stats().fec_ok,rx.stats().fec_fail,
      rx.frames_stats().flags,rx.frames_stats().frames_crc_ok,rx.frames_stats().frames_crc_bad);
    assert(seen.frames>=1);
  }
  const auto aoa=make_bits(true);
  Receiver acars(105000);Seen decode{};
  acars.set_callback(on_frame,&decode);
  const auto waveform=modulated(aoa,105000,18.f);
  for(auto s:waveform)acars.process_sample(s.i,s.q);
  assert(decode.frames==1 && decode.acars==1);
  std::puts("vdl2_receiver_test passed (synthetic IQ to AVLC and ACARS)");
}

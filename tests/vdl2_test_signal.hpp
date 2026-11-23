// SPDX-License-Identifier: AGPL-3.0-only
// Test-only synthetic VDL Mode 2 transmitter: AVLC frame bits (optionally carrying ACARS) -> D8PSK IQ at a chosen rate.
// Shared by the VDL2 receiver-chain tests and the dashboard receiver-adapter tests. Never compiled into firmware.
#pragma once
#include "vdl2_channel.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
namespace vdl2_test {
using namespace orcsdr::vdl2;
constexpr double kPi=3.14159265358979323846;
constexpr uint8_t kUw[]={0,3,2,4,0,1,6,4,1,7,2,5,6,5,7,3};
constexpr uint8_t kForward[]={0,7,3,4,1,6,2,5};
constexpr uint8_t kH[5][20]={{0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1},
 {0,0,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1},
 {1,1,0,0,0,1,1,1,0,0,1,1,0,0,0,0,1,1,1,1},
 {1,1,0,1,1,0,1,1,0,1,0,1,0,0,1,1,0,0,1,1},
 {0,1,1,0,1,0,0,1,1,1,1,0,0,1,0,1,0,1}};
inline void push_bytes(std::vector<uint8_t>& bits,const std::vector<uint8_t>& bytes){
  unsigned ones=0;
  for(uint8_t b:bytes)for(unsigned i=0;i<8;++i){
    const uint8_t bit=(b>>i)&1u;bits.push_back(bit);
    ones=bit?ones+1u:0;
    if(ones==5){bits.push_back(0);ones=0;}
  }
}
// is_acars: an ACARS-over-AVLC information frame. raw_data: an information frame that is not ACARS (stands for ATN/CPDLC/ADS-C).
// Neither: a supervisory frame with no data.
inline std::vector<uint8_t> make_bits(bool is_acars=false, bool raw_data=false) {
  std::vector<uint8_t> frame{0x21,0x43,0x65,0x87,0x11,0x23,0x45,0x67,0x01,0x36,0x75,0xFF};
  if(raw_data){
    frame.resize(8);frame.insert(frame.end(),{0x00,0x82,0x80,0xAA,0x55,0x01,0x02});
  }
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
inline std::vector<ComplexSample> modulated(const std::vector<uint8_t>& bits,uint32_t rate,float cfo){
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
} // namespace vdl2_test

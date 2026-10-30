#include "vdl2_rs.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
using namespace orcsdr::vdl2;
int main(){
  RsCodec rs;
  std::array<uint8_t,249> data{};
  for(size_t i=0;i<data.size();++i)data[i]=uint8_t((i*29+17)&255);
  uint8_t p[6]{};rs.encode(data.data(),p);
  uint8_t cw[255]{};std::memcpy(cw,data.data(),249);std::memcpy(cw+249,p,6);
  size_t corrected=55;assert(rs.correct(cw,nullptr,0,corrected)&&corrected==0);
  for(unsigned nerr=1;nerr<=3;++nerr){
    std::memcpy(cw,data.data(),249);std::memcpy(cw+249,p,6);
    for(unsigned i=0;i<nerr;++i)cw[20+61*i]^=uint8_t(i+7);
    assert(rs.correct(cw,nullptr,0,corrected)&&corrected==nerr);
    assert(std::memcmp(cw,data.data(),249)==0);
  }
  const size_t bits_sizes[]={128,320,1600,1992,2000,4000,16000};
  for(size_t bits:bits_sizes){
    std::array<uint8_t,2000> src{};
    for(size_t i=0;i<(bits+7)/8;++i)src[i]=uint8_t(i*7+13);
    std::array<uint8_t,(kMaxVdl2TransmittedBits+7)/8> wire{};
    size_t wirebits=0;
    assert(rs.interleave(src.data(),bits,wire.data(),wire.size()*8,wirebits));
    assert(wirebits==RsCodec::tx_bits_for(bits));
    std::array<uint8_t,2000> dst{};
    FecResult fr{};
    assert(rs.deinterleave(wire.data(),wirebits,bits,dst.data(),dst.size(),fr));
    assert(fr.data_bits==bits && fr.corrected_symbols==0);
    assert(std::memcmp(dst.data(),src.data(),(bits+7)/8)==0);
  }
  std::puts("vdl2_rs_test passed (synthetic RS correction and shortened multi-row interleaving)");
}

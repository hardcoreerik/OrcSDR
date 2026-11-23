// SPDX-License-Identifier: AGPL-3.0-only
// Synthetic unit vectors: NOT a validated off-air VDL2 receiver.
#include "vdl2_burst.hpp"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
using namespace orcsdr::vdl2;
namespace {
constexpr uint32_t kMasks[5] = {0x01fff0u,0x07e1fe8u,0x18e61e4u,0x1b6a662u,0x0d3caa1u};
bool parity(uint32_t x) { bool p=false;for(;x;x&=(x-1))p=!p;return p; }
bool codeword_valid(uint32_t word) {
  for(auto mask:kMasks)if(parity(word & mask))return false;
  return true;
}
uint32_t reverse17(uint32_t in) { uint32_t out=0;for(int i=0;i<17;++i){out=out*2u+(in&1u);in>>=1;}return out; }
uint32_t make_header(uint32_t length) {
  const uint32_t body=reverse17(length) << 5;
  for(uint32_t fec=0;fec<32;++fec)
    if(codeword_valid(body | fec))return body | fec;
  assert(false);return 0;
}
}
int main() {
  // Independent known output of reference LFSR 0x6959, with all-zero input.
  Descrambler15 d;
  uint8_t known[4]{};
  for(unsigned i=0;i<32;++i) known[i/8] |= d.apply_bit(0) << (i%8);
  assert(known[0]==0xc8 && known[1]==0xd8 && known[2]==0x23 && known[3]==0xa4);
  assert(d.state()==0x5211);
  Descrambler15 inverse;
  for(unsigned i=0;i<32;++i) assert(inverse.apply_bit((known[i/8] >> (i%8)) & 1u)==0);
  inverse.reset();assert(inverse.state()==kScramblerSeed);
  BurstHeader decoded;
  for(uint32_t size : {1u,100u,1000u,8191u,16383u,131071u}) {
    const uint32_t raw=make_header(size);
    assert(decode_burst_header(raw,decoded));
    assert(decoded.data_bits==size && decoded.corrected_header_bits==0 && decoded.syndrome==0);
    for(unsigned bit=0;bit<22;++bit) {
      assert(decode_burst_header(raw ^ (1u<<bit),decoded));
      assert(decoded.data_bits==size && decoded.corrected_header_bits==1 && decoded.syndrome!=0);
    }
    assert(!decode_burst_header(raw | (1u<<24),decoded));
    assert(!decode_burst_header(raw ^ 0x3u,decoded)); // syndrome 0x03 has no single-bit solution
  }
  assert(final_block_rs_parity(0)==0 && final_block_rs_parity(2)==0);
  assert(final_block_rs_parity(3)==2 && final_block_rs_parity(30)==2);
  assert(final_block_rs_parity(31)==4 && final_block_rs_parity(67)==4);
  assert(final_block_rs_parity(68)==6 && final_block_rs_parity(249)==6);
  const uint8_t unpacked[16]={1,0,0,1,0,0,0,0,1,1,1,1,1,1,1,1};
  uint8_t packed[2]={};
  assert(pack_lsb_first(unpacked,16,packed,2));
  assert(packed[0]==0x09 && packed[1]==0xff);
  assert(!pack_lsb_first(unpacked,15,packed,2));
  assert(!pack_lsb_first(unpacked,16,packed,1));
  std::puts("vdl2_burst_test passed (synthetic header, LFSR, shortened parity and bit order)");
}

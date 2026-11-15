// Independent transmit-side test oracles for receive-only metadata decoders.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_digital_voice_rx/metadata.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace orcsdr::digital_voice_rx;
#define REQUIRE(p) do {if(!(p)) {std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#p); std::exit(2);}} while(0)
namespace {
// These are *transmit-side test functions* and are not linked into
// production. They independently compose the published coding stages.
constexpr std::uint16_t parity_rows[12]={0xC75,0x63B,0xF68,0x7B4,0x3DA,0xD99,
  0x6CD,0x367,0xDC6,0xA97,0x93E,0x8EB};
std::uint16_t ccitt_msb(const std::uint8_t* bytes,std::size_t count){
 std::uint16_t c=0;
 for(std::size_t i=0;i<count;++i){ c^=static_cast<std::uint16_t>(bytes[i])<<8;
  for(int k=0;k<8;++k)c=static_cast<std::uint16_t>((c<<1)^((c&0x8000)?0x1021:0)); }
 return static_cast<std::uint16_t>(~c);
}
std::uint16_t x25(const std::uint8_t* bytes,std::size_t count){
 std::uint16_t c=0xffff;
 for(std::size_t i=0;i<count;++i) {c^=bytes[i];for(int k=0;k<8;++k)c=static_cast<std::uint16_t>((c>>1)^((c&1)?0x8408:0));}
 return static_cast<std::uint16_t>(~c);
}
std::uint32_t golay(std::uint16_t data){
 std::uint16_t parity=0;
 for(unsigned row=0;row<12;++row)if(data&(1U<<(11-row)))parity^=parity_rows[row];
 return (static_cast<std::uint32_t>(data)<<12)|parity;
}
std::vector<std::uint8_t> fich_encoded(std::uint32_t info,bool poison_crc=false,unsigned golay_flips=0){
 std::array<std::uint8_t,6> plain{
  static_cast<std::uint8_t>(info>>24),static_cast<std::uint8_t>(info>>16),
  static_cast<std::uint8_t>(info>>8),static_cast<std::uint8_t>(info)};
 std::uint16_t crc=ccitt_msb(plain.data(),4);
 if(poison_crc)crc^=0x8000;
 plain[4]=static_cast<std::uint8_t>(crc>>8);plain[5]=static_cast<std::uint8_t>(crc);
 std::vector<std::uint8_t> raw;
 for(auto b:plain)for(int j=7;j>=0;--j)raw.push_back((b>>j)&1);
 std::vector<std::uint8_t> encoded;
 for(int block=0;block<4;++block){
  std::uint16_t word=0;
  for(int k=0;k<12;++k)word=static_cast<std::uint16_t>((word<<1)|raw[block*12+k]);
  const auto code=golay(word);
  for(int k=23;k>=0;--k)encoded.push_back((code>>k)&1);
 }
 for(unsigned i=0;i<golay_flips;++i)encoded[(i*5U)%24U]^=1U;
 for(int i=0;i<4;++i)encoded.push_back(0);
 std::vector<std::uint8_t> convolution;
 std::uint8_t memory=0;
 for(auto bit:encoded){
  convolution.push_back(static_cast<std::uint8_t>(bit^((memory>>2)&1)^((memory>>3)&1)));
  convolution.push_back(static_cast<std::uint8_t>(bit^(memory&1)^((memory>>1)&1)^((memory>>3)&1)));
  memory=static_cast<std::uint8_t>(((memory<<1)|bit)&15);
 }
 REQUIRE(convolution.size()==200);
 std::vector<std::uint8_t> onair(200);
 for(unsigned slot=0;slot<100;++slot){
  const unsigned source = ((slot % 20U) * 5U + slot/20U) * 2U;
  onair[2*slot]=convolution[source];onair[2*slot+1]=convolution[source+1];
 }
 return onair;
}
std::vector<std::uint8_t> ysf_frame(std::uint32_t info, bool bad_crc=false){
 std::vector<std::uint8_t> bits;
 for(int i=39;i>=0;--i)bits.push_back((ysf::kFrameSync40>>i)&1);
 auto fich=fich_encoded(info,bad_crc);bits.insert(bits.end(),fich.begin(),fich.end());
 for(unsigned i=0;i<720;++i)bits.push_back((i*13U+i/17U)&1U);
 REQUIRE(bits.size()==960);
 return bits;
}
std::array<std::uint8_t,41> make_dstar_header(){
 std::array<std::uint8_t,41> bytes{};
 auto write=[&](int start,const char* text,std::size_t size){
  REQUIRE(std::strlen(text)<=size);
  for(std::size_t i=0;i<size;++i)bytes[start+i]=i<std::strlen(text)?static_cast<std::uint8_t>(text[i]):' ';
 };
 bytes[0]=0x40; // repeater route; no encrypted-voice claim
 write(3,"W7REPT B",8); write(11,"W7REPT C",8);
 write(19,"CQCQCQ",8);write(27,"K7TEST",8);write(35,"ORC",4);
 const auto checksum=x25(bytes.data(),39);
 bytes[39]=static_cast<std::uint8_t>(checksum);
 bytes[40]=static_cast<std::uint8_t>(checksum>>8);
 return bytes;
}
std::vector<std::uint8_t> dstar_encoded(const std::array<std::uint8_t,41>& bytes){
 std::vector<std::uint8_t> decoded;
 for(auto byte:bytes)for(int j=0;j<8;++j)decoded.push_back((byte>>j)&1);
 decoded.push_back(0);decoded.push_back(0);
 REQUIRE(decoded.size()==330);
 std::vector<std::uint8_t> trellis;
 std::uint8_t memory=0;
 for(auto b:decoded){
  trellis.push_back(static_cast<std::uint8_t>(b^(memory&1)^((memory>>1)&1)));
  trellis.push_back(static_cast<std::uint8_t>(b^((memory>>1)&1)));
  memory=static_cast<std::uint8_t>(((memory<<1)|b)&3);
 }
 REQUIRE(trellis.size()==660);
 std::vector<std::uint8_t> interleaved;
 for(unsigned row=0;row<24;++row)
  for(unsigned col=0;col<28;++col){auto i=row+24U*col;if(i<660)interleaved.push_back(trellis[i]);}
 REQUIRE(interleaved.size()==660);
 std::uint8_t state=0x7f;
 for(auto& b:interleaved){
  const auto pn=(state>>6)&1;
  const auto fb=((state>>6)^(state>>3))&1;
  state=static_cast<std::uint8_t>(((state<<1)|fb)&0x7f);
  b=static_cast<std::uint8_t>(b^pn);
 }
 return interleaved;
}
std::vector<std::uint8_t> dstar_frame(const std::array<std::uint8_t,41>& bytes){
 std::vector<std::uint8_t> bits;
 for(unsigned n=0;n<64;++n)bits.push_back((n+1)&1);
 for(int n=14;n>=0;--n)bits.push_back((dstar::kFrameSync15>>n)&1);
 auto header=dstar_encoded(bytes);bits.insert(bits.end(),header.begin(),header.end());
 return bits;
}
}
int main(){
 // Independent textbook CRC-16/X.25 check vector (RFC 1662).
 REQUIRE(x25(reinterpret_cast<const std::uint8_t*>("123456789"),9)==0x906e);
 // Official extended Golay code has min distance exactly 8.
 for(unsigned i=1;i<4096;++i) REQUIRE(__builtin_popcount(golay(static_cast<std::uint16_t>(i)))>=8);
 const std::uint32_t ysf_info=(1U<<30)|(2U<<28)|(1U<<24)|(2U<<22)|
    (3U<<19)|(4U<<16)|(1U<<14)|(2U<<11)|(2U<<8)|45U;
 auto ysf_frame_bits=ysf_frame(ysf_info);
 ysf::Fich result{};
 REQUIRE(ysf::decode_fich(ysf_frame_bits.data()+40,result));
 REQUIRE(result.raw==ysf_info && result.frame_type==1 && result.callsign_type==2);
 REQUIRE(result.frame_number==3 && result.frame_total==4 && result.data_type==2);
 REQUIRE(result.squelch_code==45 && result.golay_bit_corrections==0 && result.viterbi_distance==0);
 // Independently encoded invalid checksum must never publish FICH.
 auto fec_corrected=fich_encoded(ysf_info,false,3);
 REQUIRE(ysf::decode_fich(fec_corrected.data(),result));
 REQUIRE(result.raw==ysf_info && result.golay_bit_corrections==3);
 auto fec_uncorrectable=fich_encoded(ysf_info,false,4);
 REQUIRE(!ysf::decode_fich(fec_uncorrectable.data(),result));
 auto wrong_ysf=fich_encoded(ysf_info,true);
 REQUIRE(!ysf::decode_fich(wrong_ysf.data(),result));
 auto wrong_input=fich_encoded(ysf_info);wrong_input[0]=2;
 REQUIRE(!ysf::decode_fich(wrong_input.data(),result));
 auto multi_frame=ysf_frame_bits;
 auto second=ysf_frame(ysf_info ^ (1U<<30));multi_frame.insert(multi_frame.end(),second.begin(),second.end());
 ysf::Scanner ysf_scan;
 std::uint64_t offset=UINT64_MAX;
 unsigned matches=0;
 for(auto bit:multi_frame){if(ysf_scan.push(bit,result,offset)){REQUIRE(offset==static_cast<std::uint64_t>(matches*960));++matches;}}
 REQUIRE(matches==2);
 std::printf("YSF_OK frames=%u Golay_minimum_distance=8 FICH_CRC_verified=yes\n",matches);

 auto raw=make_dstar_header();
 auto dstar_bits=dstar_frame(raw);
 dstar::Header header{};
 REQUIRE(dstar::decode_header(dstar_bits.data()+79,header));
 REQUIRE(std::strcmp(header.my.data(),"K7TEST  ")==0);
 REQUIRE(std::strcmp(header.ur.data(),"CQCQCQ  ")==0);
 REQUIRE(header.viterbi_distance==0 && header.via_repeater);
 dstar::Scanner dstar_scan;
 offset=UINT64_MAX;matches=0;
 for(auto bit:dstar_bits){if(dstar_scan.push(bit,header,offset)){++matches;REQUIRE(offset==64);}}
 REQUIRE(matches==1);
 // Encoded payload with intentionally wrong FCS cannot create a callsign.
 raw[0]^=0x08; // recompute neither CRC nor source identity
 auto bad=dstar_encoded(raw);
 REQUIRE(!dstar::decode_header(bad.data(),header));
 raw=make_dstar_header();raw[4]=0x00;const auto crc=x25(raw.data(),39);
 raw[39]=static_cast<std::uint8_t>(crc);raw[40]=static_cast<std::uint8_t>(crc>>8);
 bad=dstar_encoded(raw);
 REQUIRE(!dstar::decode_header(bad.data(),header));
 // Convolutional encoding is correctable for a small channel error.
 auto correctable=dstar_encoded(make_dstar_header());correctable[42]^=1U;
 REQUIRE(dstar::decode_header(correctable.data(),header));
 REQUIRE(header.viterbi_distance>0);
 std::uint32_t rng=0x5c12e0a3;
 for(unsigned i=0;i<300000;++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
  auto bit=static_cast<std::uint8_t>(rng&1U);
  REQUIRE(!ysf_scan.push(bit,result,offset));
  REQUIRE(!dstar_scan.push(bit,header,offset));
 }
 std::printf("DSTAR_OK header_fcs=valid source=%s destination=%s\n",header.my.data(),header.ur.data());
 std::puts("NEGATIVE_OK invalid_fich_crc invalid_dstar_crc invalid_callsign 300000_random_bits_no_false_metadata");
 return 0;
}

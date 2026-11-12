// Independent ETSI-derived bit construction and host regression tests.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/d1.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace orcsdr::dmr;
using namespace orcsdr::dmr::d1;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return false; } } while (0)

namespace {
using Bytes12 = std::array<std::uint8_t, 12>;
using Burst = std::array<std::uint8_t, 132>;
// Test constructor owns its own matrix-derived bit positions, not D1's encoder.
constexpr std::uint16_t golay_p[8] = {0x3da,0xd99,0x6cd,0x367,0xdc6,0xa97,0x93e,0x8eb};
constexpr std::uint8_t h15[11] = {9,13,15,14,7,10,5,11,12,6,3};
constexpr std::uint8_t h13[9] = {15,14,7,10,5,11,12,6,3};
constexpr std::uint8_t h16[11] = {19,26,31,28,14,21,11,22,25,13,7};
constexpr std::uint16_t qrp[7] = {0x4f,0x11e,0x1b7,0x1e2,0x1c9,0xe5,0x73};
constexpr std::uint8_t rs_matrix[9][3] = {
  {0x1c,0xbc,0xfd},{0x89,0x31,0x08},{0xad,0x41,0x36},
  {0x7d,0x71,0x16},{0xf3,0xa6,0x3a},{0x08,0x83,0x7b},
  {0x3f,0x6f,0x02},{0x6c,0x0d,0xa7},{0x0e,0x38,0x40}
};
std::uint8_t gf(std::uint8_t a, std::uint8_t b) {
  std::uint8_t z=0;
  for (unsigned i=0;i<8;++i) { if (b&1) z^=a; b>>=1; const bool bit=a&0x80;
     a=static_cast<std::uint8_t>((a<<1U)^(bit?0x1d:0)); }
  return z;
}
void bit(std::uint8_t* data,std::size_t i,bool v) {
  const std::uint8_t m=static_cast<std::uint8_t>(1U<<(7U-i%8));
  if(v)data[i/8]|=m;else data[i/8]&=static_cast<std::uint8_t>(~m);
}
bool bitat(const std::uint8_t*data,std::size_t i){return ((data[i/8]>>(7-i%8))&1U)!=0;}
unsigned parity(const std::uint8_t* data,unsigned n,const std::uint8_t* m) {
 unsigned p=0;for(unsigned j=0;j<n;++j)if(data[j])p^=m[j];return p;
}
std::uint32_t golay(std::uint8_t u) {
  unsigned r=0;for(unsigned j=0;j<8;++j)if(u&(0x80U>>j))r^=golay_p[j];
  return (static_cast<std::uint32_t>(u)<<12U)|r;
}
std::uint16_t qr(std::uint8_t u) {
 unsigned p=0;for(unsigned i=0;i<7;++i)if(u&(0x40U>>i))p^=qrp[i];
 return static_cast<std::uint16_t>((u<<9U)|p);
}
Bytes12 make_lc(std::uint32_t dest,std::uint32_t src,unsigned flco=0,unsigned type=1){
  Bytes12 r{};
  r[0]=static_cast<std::uint8_t>(flco);r[1]=0;r[2]=0x03;
  r[3]=static_cast<std::uint8_t>(dest>>16);r[4]=static_cast<std::uint8_t>(dest>>8);
  r[5]=static_cast<std::uint8_t>(dest);r[6]=static_cast<std::uint8_t>(src>>16);
  r[7]=static_cast<std::uint8_t>(src>>8);r[8]=static_cast<std::uint8_t>(src);
  for (unsigned c=0;c<3;++c) {
    unsigned x=0;for(unsigned j=0;j<9;++j)x^=gf(r[j],rs_matrix[j][c]);
    r[9+c]=static_cast<std::uint8_t>(x^(type==1?0x96U:0x99U));
  }
  return r;
}
BptcWire encode196(const Bytes12& info){
  std::uint8_t matrix[13][15]{};
  for(unsigned i=0;i<96;++i)matrix[(i+3)/11][(i+3)%11]=bitat(info.data(),i);
  for(unsigned r=0;r<9;++r){const auto p=parity(matrix[r],11,h15);
    for(unsigned b=0;b<4;++b)matrix[r][11+b]=(p>>(3-b))&1U;}
  for(unsigned c=0;c<15;++c){std::uint8_t col[9]{};for(unsigned r=0;r<9;++r)col[r]=matrix[r][c];
    const auto p=parity(col,9,h13);
    for(unsigned b=0;b<4;++b)matrix[9+b][c]=(p>>(3-b))&1U;}
  BptcWire raw{};for(unsigned r=0;r<13;++r)for(unsigned c=0;c<15;++c)
    bit(raw.data(),1+r*15+c,matrix[r][c]!=0);
  BptcWire tx{}; if(!bptc_interleave(raw,tx))std::abort();return tx;
}
Burst make_burst(const Bytes12& data, SyncKind kind, unsigned colour, unsigned dtype) {
  std::array<std::uint8_t,264> bits{};
  const auto bptc=encode196(data);
  for(unsigned i=0;i<196;++i){unsigned pos=i<98?i:i+68;bits[pos]=bitat(bptc.data(),i);}
  const auto st=golay(static_cast<std::uint8_t>((colour<<4U)|dtype));
  for(unsigned i=0;i<10;++i)bits[98+i]=(st>>(19-i))&1U;
  for(unsigned i=0;i<10;++i)bits[156+i]=(st>>(9-i))&1U;
  const auto sync=sync_word(kind);
  for(unsigned i=0;i<48;++i)bits[108+i]=(sync>>(47-i))&1U;
  Burst out{};for(unsigned i=0;i<132;++i)out[i]=static_cast<std::uint8_t>((bits[2*i]<<1U)|bits[2*i+1]);
  return out;
}
Burst voice_sync(SyncKind kind){
  Burst out{};const auto sync=sync_word(kind);
  for(unsigned i=0;i<24;++i)out[54+i]=static_cast<std::uint8_t>((sync>>(46-2*i))&3U);
  return out;
}
std::array<std::uint8_t,12> cach(std::uint8_t slot){
  CachWire word{};CachFields f{};f.tdma_channel=(slot==2);f.payload=0x145A;
  if(!pack_cach(f,word))std::abort();
  std::array<std::uint8_t,12> d{};for(unsigned i=0;i<12;++i)d[i]=static_cast<std::uint8_t>((word[i/4]>>(6-2*(i%4)))&3U);
  return d;
}
void add(std::vector<std::uint8_t>& stream,const std::array<std::uint8_t,12>& prefix,const Burst& burst){
  stream.insert(stream.end(),prefix.begin(),prefix.end());stream.insert(stream.end(),burst.begin(),burst.end());
}
void collect(const Event& e,void* user) noexcept {
  static_cast<std::vector<Event>*>(user)->push_back(e);
}
std::vector<Event> run(const std::vector<std::uint8_t>& data,std::size_t chunk=1000000) {
  std::vector<Event> events;Decoder d;
  for(std::size_t i=0;i<data.size();i+=chunk){const auto n=(i+chunk<data.size()?chunk:data.size()-i);
    if(!d.feed(data.data()+i,n,collect,&events))std::abort(); }
  return events;
}
std::vector<std::uint8_t> to_vec(const Burst& b){return {b.begin(),b.end()};}

bool test_golay_matrix() {
  unsigned mindist=21;
  for(unsigned i=0;i<256;++i) {
      std::uint8_t c=0,d=0,err=99;
    CHECK(decode_slot_type(golay(static_cast<std::uint8_t>(i)),c,d,err));
    CHECK(c==(i>>4U)); CHECK(d==(i&15U)); CHECK(err==0);
    for(unsigned j=i+1;j<256;++j) {
      auto x=golay(static_cast<std::uint8_t>(i))^golay(static_cast<std::uint8_t>(j));
      unsigned h=0;while(x){h+=x&1U;x>>=1U;}if(h<mindist)mindist=h;
    }
  }
  CHECK(mindist==8);
  CHECK(golay(0x21)==0x21e26U); // ETSI Table B.11 row parity XOR, pinned vector
  for(unsigned k=0;k<20;++k)for(unsigned j=k+1;j<20;++j)for(unsigned m=j+1;m<20;++m) {
    std::uint8_t c=0,d=0,n=0;
    CHECK(decode_slot_type(golay(0x21)^(1U<<k)^(1U<<j)^(1U<<m),c,d,n));
    CHECK(c==2&&d==1&&n==3);
  }
  for(unsigned k=0;k<17;++k) {
    std::uint8_t c=0,d=0,n=0;
    CHECK(!decode_slot_type(golay(0x21)^(15U<<k),c,d,n));
  }
  for(unsigned k=0;k<20;++k){std::uint8_t c=0,d=0,n=0;
    CHECK(decode_slot_type(golay(0x21)^(1U<<k),c,d,n)); CHECK(c==2&&d==1&&n==1);}
  for(unsigned k=0;k<20;++k)for(unsigned j=k+1;j<20;++j) {
    std::uint8_t c=0,d=0,n=0;
    CHECK(decode_slot_type(golay(0x21)^(1U<<k)^(1U<<j),c,d,n));
    CHECK(c==2&&d==1&&n==2);
  }
  std::uint8_t c=0xd,d=0xe,n=9;
  CHECK(!decode_slot_type(0x100000,c,d,n));CHECK(c==0xd&&d==0xe&&n==9);
  return true;
}
bool test_rs_and_parsing() {
  auto lc=make_lc(99,3024109);
  // Generator matrix B.18 and field mask B.21, independently constructed.
  CHECK(check_full_lc(lc,1));CHECK(!check_full_lc(lc,2));
  Event e{};CHECK(!parse_identity(lc.data(),e)); CHECK(e.identity==IdentityKind::none);
  e.integrity_valid=true;CHECK(parse_identity(lc.data(),e));
  CHECK(e.identity==IdentityKind::group&&e.destination_id==99&&e.source_id==3024109);
  lc[4]^=1;CHECK(!check_full_lc(lc,1));
  lc=make_lc(4209000,24,3,2);CHECK(check_full_lc(lc,2));
  e={};e.integrity_valid=true;CHECK(parse_identity(lc.data(),e));CHECK(e.identity==IdentityKind::private_call);
  lc=make_lc(24,4209000,17,1);e={};e.integrity_valid=true;CHECK(check_full_lc(lc,1));
  CHECK(!parse_identity(lc.data(),e)); CHECK(e.identity==IdentityKind::none);
  return true;
}
// Independent bitwise CRC polynomial recurrence (ETSI B.3.8) with a fixed
// known-input expected checksum, not the implementation-under-test helper.
std::uint16_t crc10(const Bytes12& data) {
  std::uint16_t crc=0;
  for(unsigned i=0;i<10;++i) {
    crc^=static_cast<std::uint16_t>(data[i])<<8U;
    for(unsigned bit=0;bit<8;++bit) crc=static_cast<std::uint16_t>(
      (crc<<1U)^((crc&0x8000U)!=0?0x1021U:0U));
  }
  return static_cast<std::uint16_t>(crc^0xffffU);
}
bool test_crc_blocks() {
  Bytes12 data{};for(unsigned i=0;i<10;++i)data[i]=static_cast<std::uint8_t>(i);
  CHECK(crc10(data)==0xdc87U);
  for(unsigned type: {3U,6U,11U}) {
    const auto mask=type==3?0xa5a5U:(type==6?0xccccU:0x3333U);
    const auto value=crc10(data)^mask;
    data[10]=static_cast<std::uint8_t>(value>>8U);
    data[11]=static_cast<std::uint8_t>(value);
    CHECK(check_crc16(data,static_cast<std::uint8_t>(type)));
    CHECK(!check_crc16(data,12));
    auto burst=to_vec(make_burst(data,SyncKind::ms_data,4,type));
    auto events=run(burst,1);
    CHECK(events.size()==1);
    CHECK(events[0].integrity_valid&&events[0].evidence==Evidence::check_valid);
    CHECK(events[0].identity==IdentityKind::none);
    CHECK(events[0].data_type==type&&events[0].colour_code==4);
    data[4]^=1;CHECK(!check_crc16(data,static_cast<std::uint8_t>(type)));
    data[4]^=1;
  }
  return true;
}
bool test_bptc() {
  const auto lc=make_lc(24,4209000);
  auto tx=encode196(lc);
  std::array<std::uint8_t,12> decoded{};std::uint16_t repairs=999;
  CHECK(decode_bptc(tx,decoded,repairs));CHECK(decoded==lc&&repairs==0);
  for(unsigned j: {4U,16U,68U,111U,195U}) {
    auto damaged=tx;bit(damaged.data(),j,!bitat(damaged.data(),j));
    repairs=999;CHECK(decode_bptc(damaged,decoded,repairs));
    CHECK(decoded==lc&&repairs>=1);
  }
  // End-of-packet integrity must fail even if BPTC passes: corrupt payload,
  // then generate a fully valid BPTC word around that corrupt payload.
  auto bad=lc;bad[8]^=0x10;tx=encode196(bad);
  CHECK(decode_bptc(tx,decoded,repairs));CHECK(!check_full_lc(decoded,1));
  return true;
}
bool test_stream() {
  const auto lc=make_lc(99,3024109);
  const auto bs1=make_burst(lc,SyncKind::bs_data,3,1);
  const auto bs2=make_burst(make_lc(24,4209000,3,2),SyncKind::bs_data,9,2);
  std::vector<std::uint8_t> wire{3,2,1,1,2,0,1};
  add(wire,cach(1),bs1);add(wire,cach(2),bs2);
  auto ev=run(wire);
  CHECK(ev.size()==2);CHECK(ev[0].slot==0&&ev[1].slot==0);
  CHECK(!ev[0].slot_from_cach&&!ev[1].slot_from_cach);
  CHECK(ev[0].cach_confidence==CachConfidence::tentative);
  CHECK(ev[1].cach_confidence==CachConfidence::tentative);
  CHECK(ev[0].cach_candidate_slot==1&&ev[1].cach_candidate_slot==2);
  CHECK(ev[0].identity==IdentityKind::group&&ev[0].destination_id==99);
  CHECK(ev[1].identity==IdentityKind::private_call&&ev[1].destination_id==24);
  CHECK(ev[0].evidence==Evidence::identity_valid&&ev[1].evidence==Evidence::identity_valid);
  for(const auto chunk: {1U,2U,3U,7U,13U,131U,144U}) {
    auto seq=run(wire,chunk);CHECK(seq.size()==ev.size());
    for(std::size_t i=0;i<ev.size();++i){CHECK(seq[i].burst_start_dibit==ev[i].burst_start_dibit);
      CHECK(seq[i].source_id==ev[i].source_id&&seq[i].slot==ev[i].slot);}
  }
  auto ms=to_vec(make_burst(lc,SyncKind::direct_ts2_data,1,1));ev=run(ms);
  CHECK(ev.size()==1&&ev[0].slot==2&&!ev[0].cach_present);
  CHECK(ev[0].evidence==Evidence::identity_valid);
  auto voice=to_vec(voice_sync(SyncKind::bs_voice));ev=run(voice);
  CHECK(ev.size()==1&&ev[0].evidence==Evidence::sync_only);
  CHECK(ev[0].identity==IdentityKind::none&&!ev[0].integrity_valid);
  return true;
}
bool test_errors() {
  const auto lc=make_lc(99,3024109);
  auto good=to_vec(make_burst(lc,SyncKind::ms_data,1,1));
  Decoder d;std::vector<Event> events;
  CHECK(!d.feed(nullptr,1,collect,&events));CHECK(d.dibits_consumed()==0);
  std::uint8_t invalid[3]={0,4,2};CHECK(!d.feed(invalid,3,collect,&events));
  CHECK(d.dibits_consumed()==0);
  CHECK(d.feed(good.data(),good.size(),collect,&events));CHECK(events.size()==1);
  d.reset();CHECK(d.dibits_consumed()==0);
  // One-bit sync corruption is now a *candidate* data burst, but only
  // independent Golay, BPTC and RS/CRC may promote its identity.
  good[54]^=1;events.clear();CHECK(d.feed(good.data(),good.size(),collect,&events));
  CHECK(events.size()==1 && events[0].integrity_valid &&
        events[0].evidence==Evidence::identity_valid &&
        events[0].sync_bit_errors==1);
  good[54]^=2U;events.clear();d.reset(); // Add a SECOND bit error.
  CHECK(d.feed(good.data(),good.size(),collect,&events));
  CHECK(events.empty()); // Two bad sync bits are never accepted.
  // A syntactically valid FEC block with a deliberately corrupted RS parity
  // must not become validated identity despite a genuine data sync.
  auto tampered=make_lc(99,3024109);tampered[6]^=0x01;
  auto no_rs=to_vec(make_burst(tampered,SyncKind::ms_data,2,1));
  events=run(no_rs);CHECK(events.size()==1&&events[0].bptc_valid);
  CHECK(!events[0].integrity_valid&&events[0].identity==IdentityKind::none);
  CHECK(events[0].evidence==Evidence::fec_valid);
  // A single BPTC bit corruption is repaired but never bypasses RS.
  auto repairable=to_vec(make_burst(make_lc(99,3024109),SyncKind::ms_data,2,1));
  repairable[0]^=1;
  events=run(repairable);CHECK(events.size()==1);
  CHECK(events[0].evidence==Evidence::identity_valid&&events[0].bptc_corrected_bits>0);
  CHECK(events[0].source_id==3024109);
  auto bad=to_vec(make_burst(make_lc(99,3024109,17),SyncKind::ms_data,1,1));
  events=run(bad);CHECK(events.size()==1);
  CHECK(events[0].integrity_valid&&events[0].evidence==Evidence::check_valid);
  CHECK(events[0].identity==IdentityKind::none);
  return true;
}
bool test_payload_verified_sync_tolerance() {
  // Independently generated, complete Full-LC vectors. All 48 positions
  // are tested without modifying/dressing up the actual received dibits.
  const auto lc=make_lc(99,3024109);
  for (const SyncKind kind: {SyncKind::bs_data,SyncKind::ms_data,
                              SyncKind::direct_ts1_data,SyncKind::direct_ts2_data}) {
    const auto original=to_vec(make_burst(lc,kind,1,1));
    const auto exact=run(original,1);
    CHECK(exact.size()==1&&exact[0].sync_bit_errors==0);
    for(unsigned b=0;b<48;++b) {
      auto changed=original;
      changed[54+b/2U]^=static_cast<std::uint8_t>(b%2U ? 1U : 2U);
      const auto measured=run(changed,b%2U ? 1U : 13U);
      CHECK(measured.size()==1);
      CHECK(measured[0].sync==kind&&measured[0].sync_bit_errors==1);
      CHECK(measured[0].integrity_valid &&
            measured[0].evidence==Evidence::identity_valid);
      CHECK(measured[0].source_id==3024109&&measured[0].destination_id==99);
      CHECK(measured[0].colour_code==1);
      if(kind==SyncKind::direct_ts1_data)CHECK(measured[0].slot==1);
      if(kind==SyncKind::direct_ts2_data)CHECK(measured[0].slot==2);
    }
    // Two bit errors must not fall through and become a trusted event.
    for(unsigned p: {0U,8U,23U}) {
      auto changed=original;
      changed[54+p]^=3U;
      CHECK(run(changed,7).empty());
    }
    // Valid BPTC, deliberately broken RS: fuzzy sync cannot publish even
    // a sync-only event, confidence level, talkgroup or CACH slot.
    auto corrupt_lc=lc;
    corrupt_lc[6]^=1U;
    auto invalid=to_vec(make_burst(corrupt_lc,kind,1,1));
    invalid[60]^=1U;
    CHECK(run(invalid,1).empty());
  }
  // Voice SYNC has no independent packet CRC/RS proof in this path:
  // exact match remains diagnostic, fuzzy match must not emit anything.
  auto voice=to_vec(voice_sync(SyncKind::direct_ts1_voice));
  CHECK(run(voice,1).size()==1);
  voice[54]^=1U;
  CHECK(run(voice,1).empty());
  return true;
}
std::array<std::uint8_t, 128> embedded_payload(const Bytes12& full) {
  std::uint8_t matrix[8][16]{};
  for(unsigned i=0;i<72;++i)matrix[i/11][i%11]=bitat(full.data(),i);
  unsigned sum=0;for(unsigned i=0;i<9;++i)sum+=full[i];
  const auto checksum=sum%31;
  for(unsigned i=0;i<5;++i)matrix[(72+i)/11][(72+i)%11]=static_cast<std::uint8_t>((checksum>>(4-i))&1U);
  for(unsigned r=0;r<7;++r){const auto p=parity(matrix[r],11,h16);
    for(unsigned i=0;i<5;++i)matrix[r][11+i]=static_cast<std::uint8_t>((p>>(4-i))&1U);}
  for(unsigned i=0;i<16;++i){unsigned check=0;for(unsigned r=0;r<7;++r)check^=matrix[r][i];
    matrix[7][i]=static_cast<std::uint8_t>(check);}
  std::array<std::uint8_t,128> out{};
  for(unsigned i=0;i<128;++i)out[i]=matrix[i%8][i/8];
  return out;
}
Burst voice_embedded(const std::array<std::uint8_t,128>& payload,unsigned stage,unsigned colour) {
  std::array<std::uint8_t,264> bits{};
  const auto lcss=stage==0?1U:(stage==3?2U:3U);
  const auto emb=qr(static_cast<std::uint8_t>((colour<<3U)|lcss));
  for(unsigned i=0;i<8;++i)bits[108+i]=static_cast<std::uint8_t>((emb>>(15-i))&1U);
  for(unsigned i=0;i<32;++i)bits[116+i]=payload[stage*32+i];
  for(unsigned i=0;i<8;++i)bits[148+i]=static_cast<std::uint8_t>((emb>>(7-i))&1U);
  Burst b{};for(unsigned i=0;i<132;++i)b[i]=static_cast<std::uint8_t>((bits[2*i]<<1U)|bits[2*i+1]);
  return b;
}
bool test_late_entry() {
  auto raw=make_lc(77,1234567);const auto payload=embedded_payload(raw);
  std::vector<std::uint8_t> wire;
  add(wire,cach(1),voice_sync(SyncKind::bs_voice));
  for(unsigned stage=0;stage<4;++stage){
    add(wire,cach(2),voice_sync(SyncKind::ms_voice));
    add(wire,cach(1),voice_embedded(payload,stage,7));
  }
  auto events=run(wire,5);
  unsigned identities=0;
  for(const auto& e:events) if(e.late_entry&&e.evidence==Evidence::identity_valid){
    CHECK(e.slot==1&&e.destination_id==77&&e.source_id==1234567);++identities;
  }
  CHECK(identities==1);
  // One Hamming-correctable bit in a stage CACH is NOT slot confirmation.
  auto corrupt_cach=wire;
  corrupt_cach[144+144]^=2U;events=run(corrupt_cach,1);
  for(const auto& e:events) CHECK(!e.late_entry);
  // Corrupt embedded payload check while leaving voice sync intact.
  wire[144+144+12+59]^=1U;events=run(wire,17);
  for(const auto& e:events) CHECK(!e.late_entry);
  return true;
}
} // namespace
int main(){
  struct T {const char* name;bool (*fn)();};
  constexpr T tests[]={{"golay_matrix",test_golay_matrix},{"rs_and_parsing",test_rs_and_parsing},
    {"bptc",test_bptc},{"crc_blocks",test_crc_blocks},{"dual_slot_chunk",test_stream},{"corruption",test_errors},
    {"late_entry",test_late_entry},{"payload_verified_sync_tolerance",test_payload_verified_sync_tolerance}};
  unsigned pass=0;
  for(const auto& t:tests){if(!t.fn()){std::fprintf(stderr,"FAILED: %s\n",t.name);return 1;}
    std::printf("PASS %s\n",t.name);++pass;}
  std::printf("PASS total %u\n",pass);
  return 0;
}

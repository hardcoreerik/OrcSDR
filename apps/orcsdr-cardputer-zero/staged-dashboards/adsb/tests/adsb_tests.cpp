#include "adsb_module.hpp"
#include "adsb_renderer.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace adsb=orc::zero::adsb;
namespace {
void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
constexpr uint64_t base=1'000'000'000'000ull;
const std::array<uint8_t,14> identity{{0x8d,0x48,0x40,0xd6,0x20,0x2c,0xc3,0x71,0xc3,0x2c,0xe0,0x57,0x60,0x98}};
const std::array<uint8_t,14> even{{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x82,0xd6,0x90,0xc8,0xac,0x28,0x63,0xa7}};
const std::array<uint8_t,14> odd{{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x86,0x43,0x5c,0xc4,0x12,0x69,0x2a,0xd6}};
// Deterministic 2.048 MS/s ADS-B pulse-position CU8, copied as a recipe
// (not stored fixture bytes) from the existing decoder self-check.
std::vector<uint8_t> pulse(const std::array<uint8_t,14>& bytes) {
  std::vector<uint8_t> iq(247*2,127);
  for(size_t i:{size_t{0},size_t{2},size_t{7},size_t{9}})iq[i*2]=220;
  for(int bit=0;bit<112;++bit){
    const bool one=(bytes[bit/8]&(0x80u>>(bit%8)))!=0;
    const size_t first=(16246u+size_t(bit)*2048u+500u)/1000u;
    const size_t second=(16246u+size_t(bit)*2048u+1024u+500u)/1000u;
    iq[(one?first:second)*2]=220;
  }
  return iq;
}
// The same idealized ADS-B pulse schedule sampled at the FM rate (2.56 MS/s).
// A detector whose fixed interpolation stride is 2.048 MS/s must NOT decode it.
std::vector<uint8_t> wrong_rate_pulse(const std::array<uint8_t,14>& bytes) {
  std::vector<uint8_t> iq(310*2,127);
  auto scale=[](uint64_t index_milli) {return size_t((index_milli*5+2000)/4000);};
  for(size_t p:{size_t{0},size_t{2},size_t{7},size_t{9}})
    iq[std::min(size_t(309),(p*5+2)/4)*2]=220;
  for(int bit=0;bit<112;++bit){
    const bool one=(bytes[bit/8]&(0x80u>>(bit%8)))!=0;
    const uint64_t center=16246u+uint64_t(bit)*2048u+(one?0:1024u);
    const auto index=std::min(size_t(309),scale(center));
    iq[index*2]=220;
  }
  return iq;
}
orc::linux_sdr::IqMetadata meta(uint64_t t,uint64_t sample=0,uint64_t gen=1){
  orc::linux_sdr::IqMetadata m{};m.frequency_hz=adsb::kCenterHz;m.sample_rate_sps=adsb::kRateSps;
  m.receiver_generation=gen;m.monotonic_ns=t;m.first_sample_index=sample;return m;
}
orcsdr::adsb_rx::Frame test_frame(uint32_t id,uint64_t /*time*/=0) {
  orcsdr::adsb_rx::Frame f{};f.icao=id;f.bit_length=112;f.signal=40;return f;
}
void test_real_decoder() {
  adsb::Controller c;check(c.activate(1,base),"activate");c.receiver_ready(base);
  auto iq=pulse(identity);auto m=meta(base+1);
  check(c.consume_cu8(iq.data(),iq.size(),m)==adsb::Result::accepted,"decoder accepts valid IQ");
  auto s=c.snapshot(base+2);check(s.count==1&&s.aircraft[0].icao==0x4840d6,"ICAO decoded");
  check(s.aircraft[0].has_callsign&&std::strcmp(s.aircraft[0].callsign,"KLM1023")==0,"callsign decoded");
  check(s.counters.crc_ok==1&&s.counters.unique_seen==1,"CRC + unique counters");
  check(c.consume_cu8(iq.data(),iq.size(),meta(base+3,247))==adsb::Result::accepted,"repeat");
  s=c.snapshot(base+4);check(s.counters.crc_ok==2&&s.counters.unique_seen==1,"duplicate does not add unique");
  // Fresh session with bad payload CRC must not produce an aircraft.
  auto bad=identity;bad[12]^=0x01u;auto rejected=pulse(bad);
  check(c.activate(2,base+5),"restart");c.receiver_ready(base+5);
  check(c.consume_cu8(rejected.data(),rejected.size(),meta(base+6,0,2))==adsb::Result::accepted,"bad IQ processed");
  s=c.snapshot(base+7);check(s.count==0&&s.counters.crc_ok==0,"CRC rejection");
  check(s.counters.candidates>=1&&s.counters.rejected>=1,"candidate is not accepted frame");
}
void test_partial_and_selection() {
  adsb::Model m;m.reset(1);
  auto a=test_frame(1);a.has_callsign=true;std::memcpy(a.callsign,"ABC123",7);
  m.apply_verified(a,base,1);auto b=test_frame(2);b.has_altitude=true;b.altitude_ft=12345;
  m.apply_verified(b,base+1,1);m.apply_verified(test_frame(1),base+2,1);
  auto s=m.snapshot(base+2,adsb::Status::receiving,false,true);
  check(s.count==2&&std::strcmp(s.aircraft[0].callsign,"ABC123")==0,"partial update retains callsign");
  check(!s.aircraft[0].has_altitude&&s.aircraft[1].has_altitude,"fields isolated by ICAO");
  m.navigate(adsb::Action::down);s=m.snapshot(base+3,adsb::Status::receiving,false,true);
  check(s.selected_icao==2,"selected second ICAO");m.navigate(adsb::Action::open_detail);
  check(m.snapshot(base+4,adsb::Status::receiving,false,true).detail,"detail navigation");
  m.expire(base+adsb::kExpireNs+2);
  s=m.snapshot(base+adsb::kExpireNs+2,adsb::Status::stale,false,true);
  check(s.count==1&&s.selected_icao==1,"expiry retains viable selection");
  m.expire(base+adsb::kExpireNs+3);s=m.snapshot(base+adsb::kExpireNs+3,adsb::Status::stale,false,true);
  check(s.count==0&&!s.detail&&s.scroll==0,"empty table clamped");
}
void test_capacity() {
  adsb::Model m;m.reset(1);
  for(uint32_t i=1;i<=adsb::kAircraftCapacity;++i)m.apply_verified(test_frame(i),base+i,1);
  auto s=m.snapshot(base+90,adsb::Status::receiving,false,true);
  check(s.count==adsb::kAircraftCapacity,"bounded capacity");
  for(size_t i=0;i<70;++i)m.navigate(adsb::Action::down);
  s=m.snapshot(base+90,adsb::Status::receiving,false,true);
  check(s.selection==31&&s.scroll==26&&s.selected_icao==32,"bounded scroll");
  m.apply_verified(test_frame(33),base+90,1);
  s=m.snapshot(base+90,adsb::Status::receiving,false,true);
  check(s.count==32&&s.aircraft[0].icao==33&&s.selected_icao==32,"oldest deterministic eviction and selected ICAO");
  for(uint32_t i=34;i<=300;++i)m.apply_verified(test_frame(i),base+90+i,1);
  s=m.snapshot(base+400,adsb::Status::receiving,false,true);
  check(s.count==32&&s.counters.unique_seen==256&&s.counters.unique_capped,
        "capped exact unique ledger does not claim unlimited identities");
}
void test_cpr() {
  // Use a native CRC-valid pair of canonical position reports, with exact
  // CPR fields from their wire bits (the values below were checked against
  // the existing self-check, not fabricated coordinates).
  auto even_f=test_frame(0x40621d),odd_f=test_frame(0x40621d);
  even_f.has_cpr=odd_f.has_cpr=true;even_f.cpr_odd=false;odd_f.cpr_odd=true;
  // Wire report values are adjusted by the exact bits below instead of
  // trusting manually chosen coordinates.
  auto get_bits=[](const auto& b,int p,int n){uint32_t out=0;for(int i=0;i<n;++i)out=(out<<1)|((b[(p+i)/8]>>(7-(p+i)%8))&1u);return out;};
  even_f.cpr_latitude=get_bits(even,54,17);even_f.cpr_longitude=get_bits(even,71,17);
  odd_f.cpr_latitude=get_bits(odd,54,17);odd_f.cpr_longitude=get_bits(odd,71,17);
  adsb::Model m;m.reset(1);m.apply_verified(even_f,base,1);
  check(!m.snapshot(base,adsb::Status::receiving,false,true).aircraft[0].has_position,"single CPR not fix");
  auto wrong=odd_f;wrong.icao=0x40621e;m.apply_verified(wrong,base+1,1);
  check(!m.snapshot(base+1,adsb::Status::receiving,false,true).aircraft[0].has_position,"cross ICAO no pair");
  m.apply_verified(odd_f,base+adsb::kCprPairNs+1,1);
  check(!m.snapshot(base+adsb::kCprPairNs+1,adsb::Status::receiving,false,true).aircraft[0].has_position,"stale CPR no fix");
  m.apply_verified(even_f,base+adsb::kCprPairNs+2,1);
  m.apply_verified(odd_f,base+adsb::kCprPairNs+3,1);
  auto s=m.snapshot(base+adsb::kCprPairNs+3,adsb::Status::receiving,false,true);
  check(s.aircraft[0].has_position,"valid CPR pair");
  check(std::abs(s.aircraft[0].latitude-52.2658)<0.002&&std::abs(s.aircraft[0].longitude-3.9389)<0.002,"CPR actual position");
  m.reset(2);m.apply_verified(odd_f,base+adsb::kCprPairNs+3,1);
  check(m.snapshot(base+adsb::kCprPairNs+3,adsb::Status::waiting,false,true).count==0,"old generation rejected");
  m.apply_verified(even_f,base+adsb::kCprPairNs+3,2);
  check(!m.snapshot(base+adsb::kCprPairNs+3,adsb::Status::waiting,false,true).aircraft[0].has_position,"no cross generation pairing");
  m.invalidate_cpr();m.apply_verified(odd_f,base+adsb::kCprPairNs+4,2);
  check(!m.snapshot(base+adsb::kCprPairNs+4,adsb::Status::waiting,false,true).aircraft[0].has_position,"no gap pairing");
}
void test_continuity_and_invalid() {
  adsb::Controller c;check(!c.activate(0,base),"invalid generation rejected");
  check(c.activate(1,base),"activate");c.receiver_ready(base);
  auto iq=pulse(identity);
  check(c.consume_cu8(iq.data(),iq.size()-1,meta(base+1))==adsb::Result::invalid_data,"odd CU8 rejected");
  auto rate=meta(base+1);rate.sample_rate_sps=2560000;
  check(c.consume_cu8(iq.data(),iq.size(),rate)==adsb::Result::invalid_radio,"FM rate rejected");
  orcsdr::adsb_rx::Decoder native;
  auto wrong=wrong_rate_pulse(identity);
  native.process_cu8(wrong.data(),wrong.size(),nullptr,nullptr);
  check(native.stats().crc_ok==0,"2.56 MS/s waveform is not 2.048 MS/s decoded");
  rate=meta(base+1);rate.frequency_hz=1091000000;
  check(c.consume_cu8(iq.data(),iq.size(),rate)==adsb::Result::invalid_radio,"wrong RF center rejected");
  check(c.consume_cu8(iq.data(),160,meta(base+1))==adsb::Result::accepted,"split part 1");
  check(c.snapshot(base+1).count==0,"no premature decode");
  check(c.consume_cu8(iq.data()+160,iq.size()-160,meta(base+2,80))==adsb::Result::accepted,"split part 2");
  check(c.snapshot(base+2).count==1,"chunk boundary decode");
  check(c.activate(2,base+3),"restart");c.receiver_ready(base+3);
  check(c.consume_cu8(iq.data(),160,meta(base+4,0,2))==adsb::Result::accepted,"split gap 1");
  auto gap=meta(base+5,80,2);gap.discontinuity=true;
  check(c.consume_cu8(iq.data()+160,iq.size()-160,gap)==adsb::Result::accepted,"split gap 2");
  check(c.snapshot(base+5).count==0,"no spliced frame across gap");
  check(c.consume_cu8(iq.data(),iq.size(),meta(base+6,247,1))==adsb::Result::old_generation,"old generation discarded");
  check(c.consume_cu8(iq.data(),iq.size(),meta(base+2,247,2))==adsb::Result::invalid_time,"time rollback rejected");
  c.disconnected();check(c.consume_cu8(iq.data(),iq.size(),meta(base+6,247,2))==adsb::Result::inactive,"disconnected rejects data");
  check(c.snapshot(base+25'000'000'000ull).status==adsb::Status::disconnected,"disconnection state");
  c.stop();check(c.snapshot(base+30'000'000'000ull).count==0,"stop flush");
  // One legal host block can exceed the native decoder's 16,640-sample
  // buffer; frame at complex sample 20,000 must still be found by chunking.
  check(c.activate(3,base+31'000'000'000ull),"big-block restart");
  c.receiver_ready(base+31'000'000'000ull);
  std::vector<uint8_t> large(32768*2,127);
  std::copy(iq.begin(),iq.end(),large.begin()+20000*2);
  check(c.consume_cu8(large.data(),large.size(),meta(base+32'000'000'000ull,0,3))==adsb::Result::accepted,"large legal CU8 block");
  check(c.snapshot(base+32'000'000'000ull).count==1,"late-in-block frame survived chunking");
}
void test_renderer() {
  adsb::Model m;m.reset(1);adsb::Renderer r;
  auto s=m.snapshot(base,adsb::Status::demo,true,false);r.render(s);
  const auto empty=r.pixels();check(r.stats().full_clears==1,"initial clear");
  auto f=test_frame(0xa29551);f.has_callsign=true;std::memcpy(f.callsign,"ASA1310",8);
  f.has_altitude=true;f.altitude_ft=35000;m.apply_verified(f,base+1,1);
  s=m.snapshot(base+2,adsb::Status::demo,true,false);r.render(s);
  check(r.stats().full_clears==1,"no full clear on update");
  check(r.pixels()!=empty,"real pixels changed on data");
  m.navigate(adsb::Action::open_detail);
  s=m.snapshot(base+3,adsb::Status::demo,true,false);r.render(s);
  check(r.pixels()!=empty&&r.stats().full_clears==1,"detail dirty update");
  check(sizeof(adsb::Renderer)<115000,"render backing memory bounded");
}
}
int main(){try{
  test_real_decoder();test_partial_and_selection();test_capacity();test_cpr();
  test_continuity_and_invalid();test_renderer();
  std::cout<<"PASS adsb_focused (decoder, CRC, CPR, continuity, 32-aircraft table, dirty render)\n";
  return 0;
}catch(const std::exception& e){std::cerr<<"FAIL adsb_focused: "<<e.what()<<"\n";return 1;}}

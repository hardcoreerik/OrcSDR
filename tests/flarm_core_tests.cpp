#include "flarm_decoder_core.hpp"
#include "flarm_receiver.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace orcsdr::flarm_rx;
struct Vector { int version; uint32_t epoch; double lat, lon; std::array<uint8_t,26> packet; };
std::vector<Vector> vectors() {
  std::ifstream in("tests/fixtures/flarm_packets.txt"); assert(in.good());
  std::string line; std::vector<Vector> result;
  while (std::getline(in,line)) {
    if (line.empty() || line[0]=='#') continue;
    Vector v{}; std::string hex; std::istringstream row(line);
    row >> v.version >> v.epoch >> v.lat >> v.lon >> hex; assert(hex.size()==48);
    for (size_t i=0;i<24;++i) v.packet[i]=uint8_t(std::stoul(hex.substr(2*i,2),nullptr,16));
    // Independent bitwise CCITT implementation, including the NRF905 address.
    uint32_t crc=0xffff;
    for (unsigned i=0;i<27;++i) {
      const uint8_t b=i<3 ? std::array<uint8_t,3>{0x31,0xfa,0xb6}[i] : v.packet[i-3];
      for(int bit=7;bit>=0;--bit) {
        const bool feedback=((crc>>15) ^ (b>>bit)) & 1;
        crc=((crc<<1) ^ (feedback ? 0x1021 : 0)) & 0xffff;
      }
    }
    v.packet[24]=uint8_t(crc>>8);v.packet[25]=uint8_t(crc);
    result.push_back(v);
  }
  assert(result.size()==8);return result;
}
Context context(const Vector& v) {
  return {uint64_t(v.epoch)*1000, int32_t(std::llround((v.lat+0.05)*1e7)),
          int32_t(std::llround((v.lon-0.05)*1e7)),true};
}
void collect(const Frame& f,void* user) { static_cast<std::vector<Frame>*>(user)->push_back(f); }
void check_frame(const Vector& v,const Frame& f) {
  assert(f.generation==v.version && f.address==0x123456 && f.address_type==2);
  assert(std::fabs(f.latitude-v.lat)<0.00004);
  assert(std::fabs(f.longitude-v.lon)<0.00012);
  assert(std::fabs(f.altitude_m-1250)<2);
  assert(std::fabs(f.speed_mps-40)<1);
  assert(f.aircraft_type==1);
  const float expected_course=v.lon>170 ? 359 : v.lat<0 ? 270 : 90;
  assert(std::fabs(f.course_deg-expected_course)<1.5);
  const bool stealth=v.epoch==1788868799;
  assert(f.stealth==stealth && f.no_track==stealth);
  assert(std::fabs(f.climb_mps-(stealth ? 0 : -2.4f))<0.2);
}
// Synthetic continuous-phase 2FSK with RF payload inversion and Manchester.
// The packet bytes come from SoftRF, not this decoder's crypto implementation.
std::vector<uint8_t> waveform(const Vector& v,int channel,double offset=0,bool inverted=false,
                              double chip_rate=100000,double noise=0.015) {
  std::vector<bool> chips;
  for(unsigned i=0;i<16;++i) chips.push_back(i&1);
  constexpr uint64_t sync=0x5599a5a955666596ULL;
  for(int i=63;i>=0;--i) chips.push_back((sync>>i)&1);
  for(uint8_t b:v.packet) for(int i=7;i>=0;--i) {
    const bool bit=(b>>i)&1;chips.push_back(bit);chips.push_back(!bit);
  }
  const size_t count=size_t((chips.size()+40)*kSampleRate/chip_rate)+100;
  std::vector<uint8_t> iq; iq.reserve(2*count);
  double phase=0;
  std::mt19937 rng(731);std::normal_distribution<double> normal(0,noise);
  for(size_t n=0;n<count;++n) {
    const int symbol=int((double(n)-63.7)*chip_rate/kSampleRate);
    const bool active=double(n)>=63.7 && symbol>=0 && size_t(symbol)<chips.size();
    const bool mark=active && (chips[size_t(symbol)]!=inverted);
    const double f=(channel ? 100000 : -100000)+offset+(mark ? 50000 : -50000);
    phase+=2*3.14159265358979323846*f/kSampleRate;
    for (double sample : {active ? 0.65*std::cos(phase) : 0,active ? 0.65*std::sin(phase) : 0})
      iq.push_back(uint8_t(std::clamp(std::lround(127.5+128*(sample+normal(rng))),0L,255L)));
  }
  return iq;
}
void feed(Decoder& d,const std::vector<uint8_t>& iq,const Context& c,std::vector<Frame>& got,size_t chunk) {
  for(size_t b=0;b<iq.size();b+=chunk) {
    auto ctx=c;ctx.unix_ms+=b*500/kSampleRate;
    d.process_cu8(iq.data()+b,std::min(chunk,iq.size()-b),ctx,collect,&got);
  }
}
int main() {
  const auto all=vectors();
  for(const auto& v:all) {
    Frame f;auto ctx=context(v);
    assert(decode_packet(v.packet.data(),26,ctx,&f)==Result::ok);check_frame(v,f);
    auto copy=v.packet;
    for(size_t bit=0;bit<208;++bit) {
      copy=v.packet;copy[bit/8]^=uint8_t(1u<<(bit%8));
      assert(decode_packet(copy.data(),26,ctx,&f)==Result::crc_error);
    }
    assert(decode_packet(v.packet.data(),25,ctx,&f)==Result::invalid);
    ctx.unix_ms=0;assert(decode_packet(v.packet.data(),26,ctx,&f)==Result::need_time);
    ctx=context(v);ctx.location_valid=false;
    assert(decode_packet(v.packet.data(),26,ctx,&f)==Result::need_location);
    ctx=context(v);ctx.unix_ms+=1000;
    assert(decode_packet(v.packet.data(),26,ctx,&f)==Result::ok);check_frame(v,f);
  }
  for(size_t vi=0;vi<2;++vi) for(int channel=0;channel<2;++channel)
    for(bool inverted:{false,true}) for(double offset:{-5000.0,0.0,5000.0}) {
      const auto& v=all[vi];Decoder d;std::vector<Frame> got;
      auto iq=waveform(v,channel,offset,inverted,100003);
      feed(d,iq,context(v),got,137); // odd chunk sizes exercise split I/Q pairs
      if(got.size()!=1) {
        std::fprintf(stderr,"IQ failed v%d ch%d inv%d off%.0f: got %zu sync %u crc %u/%u invalid %u\n",
                     v.version,channel,inverted,offset,got.size(),d.stats().syncs,d.stats().crc_ok,d.stats().crc_errors,d.stats().invalid);
        return 1;
      }
      assert(got[0].channel==channel);check_frame(v,got[0]);
    }
  // Overlapping V6 and V7 packets on both channels, one tuning window.
  auto left=waveform(all[0],0,0,false,100000,0);
  auto right=waveform(all[1],1,0,false,100000,0);
  for(size_t i=0;i<left.size();++i) left[i]=uint8_t(std::clamp(127+(int(left[i])-127)/2+(int(right[i])-127)/2,0,255));
  Decoder d;std::vector<Frame> got;feed(d,left,context(all[0]),got,4096);
  assert(got.size()==2);
  d.reset();got.clear();
  std::mt19937 rng(97);std::vector<uint8_t> noise(2*kSampleRate);
  for(auto& b:noise)b=uint8_t(rng());
  feed(d,noise,context(all[0]),got,32768);assert(got.empty());
  // An IQ gap must not join the beginning and end of an otherwise valid packet.
  const auto iq=waveform(all[1],0);d.reset();got.clear();
  d.process_cu8(iq.data(),iq.size()/2,context(all[1]),collect,&got);
  d.discontinuity();
  d.process_cu8(iq.data()+iq.size()/2,iq.size()-iq.size()/2,context(all[1]),collect,&got);
  assert(got.empty());
  assert(orcsdr::flarm::initialize());
  const auto reference = context(all[1]);
  orcsdr::flarm::set_reference(true, reference.latitude_e7, reference.longitude_e7);
  orcsdr::flarm::process(iq.data(), iq.size(), 1, 0, reference.unix_ms, 1000);
  auto snapshot = orcsdr::flarm::snapshot(1, 1000);
  assert(snapshot.aircraft_count == 1 && snapshot.flarm_v7 == 1);
  assert(snapshot.aircraft[0].icao == 0x123456 && snapshot.aircraft[0].protocol_generation == 7);
  assert(orcsdr::flarm::snapshot(2, 1000).aircraft_count == 0);
  assert(orcsdr::flarm::snapshot(1, 31000).aircraft_count == 0);
  orcsdr::flarm::process(iq.data(), iq.size(), 2, 1, reference.unix_ms, 2000);
  assert(orcsdr::flarm::snapshot(2, 2000).flarm_v7 == 1);
  const auto legacy_iq = waveform(all[0], 0);
  orcsdr::flarm::process(legacy_iq.data(), legacy_iq.size(), 3, 2, reference.unix_ms, 3000);
  assert(orcsdr::flarm::snapshot(3, 3000).aircraft_count == 0);
  orcsdr::flarm::process(legacy_iq.data(), legacy_iq.size(), 3, 3, reference.unix_ms, 4000);
  assert(orcsdr::flarm::snapshot(3, 4000).aircraft_count == 1);
  puts("FLARM: reference V6/V7, CRC corruption, context/rollover, dual-channel IQ, inversion, CFO, noise and gaps passed");
}

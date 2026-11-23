// Synthetic IQ tests cover original DSP/HDLC/CRC pipeline, NOT on-air performance.
#include "ais_receiver.hpp"
#include "ais_receive_pipeline.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rx=orcsdr::ais_rx;
namespace {
constexpr double pi=3.14159265358979323846;
struct Results {std::vector<rx::Packet> packets;};
void collect(const rx::Packet& p,void* ctx) {static_cast<Results*>(ctx)->packets.push_back(p);}
uint16_t crc_byte(uint16_t crc,uint8_t v) {
  crc ^= v;
  for(int i=0;i<8;++i) crc = static_cast<uint16_t>((crc >> 1) ^ ((crc & 1) ? 0x8408 : 0));
  return crc;
}
uint16_t fcs(const std::vector<uint8_t>& data) {
  uint16_t crc=0xffff;
  for(auto byte:data)crc=crc_byte(crc,byte);
  return static_cast<uint16_t>(~crc);
}
void add_flag(std::vector<int>& b) {
  const int flag[8]={0,1,1,1,1,1,1,0};
  b.insert(b.end(),flag,flag+8);
}
std::vector<int> wire_bits(const std::vector<uint8_t>& payload,bool broken_crc=false) {
  std::vector<int> bits;
  for(int i=0;i<24;++i)bits.push_back(i%2);
  add_flag(bits);
  std::vector<uint8_t> bytes=payload;
  uint16_t checksum=fcs(payload);
  if(broken_crc)checksum ^= 0x4000;
  bytes.push_back(checksum & 255);bytes.push_back(checksum >> 8);
  unsigned ones=0;
  for(auto byte:bytes) {
    for(unsigned n=0;n<8;++n) {
      const int b=(byte>>n)&1;
      bits.push_back(b);
      if(b)++ones;else ones=0;
      if(ones==5){bits.push_back(0);ones=0;}
    }
  }
  add_flag(bits);
  return bits;
}
std::vector<int> nrzi_levels(const std::vector<int>& bits,bool invert=false) {
  std::vector<int> levels;
  int level=1;
  for(int b:bits) {
    if(!b)level^=1;
    levels.push_back(invert ? 1-level : level);
  }
  return levels;
}
std::vector<uint8_t> make_type1(uint32_t mmsi) {
  std::vector<uint8_t> p(21);
  auto put=[&](unsigned offset,unsigned width,uint32_t value) {
    for(unsigned bit=0;bit<width;++bit) {
      const unsigned pos=offset+bit;
      p[pos/8] |= static_cast<uint8_t>(((value>>(width-bit-1))&1u)<<(7-(pos%8)));
    }
  };
  put(0,6,1); put(8,30,mmsi); put(50,10,87);
  put(61,28,static_cast<uint32_t>(-73380000));
  put(89,27,28440000);put(116,12,1350);put(128,9,135);
  return p;
}
void verify_direct_hdlc() {
  rx::Hdlc decoder;
  auto data=make_type1(366967101);
  auto encoded=nrzi_levels(wire_bits(data));
  Results result;
  for(int level:encoded)decoder.process_level(level,1,collect,&result);
  assert(result.packets.size()==1);
  assert(result.packets[0].channel==1);
  assert(result.packets[0].bit_count==168);
  assert(std::memcmp(data.data(),result.packets[0].bytes,data.size())==0);
  assert(decoder.stats().crc_ok==1);
  decoder.reset();result.packets.clear();
  encoded=nrzi_levels(wire_bits(data,true));
  for(int level:encoded)decoder.process_level(level,1,collect,&result);
  assert(result.packets.empty());
  assert(decoder.stats().crc_failed==1);
  decoder.reset();result.packets.clear();
  encoded=nrzi_levels(wire_bits(data),true);
  for(int level:encoded)decoder.process_level(level,2,collect,&result);
  assert(result.packets.size()==1 && result.packets[0].channel==2);
  // FCS/X25 known-independent check (RFC 1662 check value).
  const std::vector<uint8_t> example={'1','2','3','4','5','6','7','8','9'};
  assert(fcs(example)==0x906e);
}
// Produce Gaussian-shaped h=0.5 continuous-phase GMSK at 25 samples/symbol.
std::vector<std::pair<float,float>> gmsk(const std::vector<int>& levels,
                                          int channel_index,double cfo_hz=0) {
  constexpr unsigned sps=25;
  const int margin=75;
  const size_t n=(levels.size()+12)*sps;
  std::vector<double> base(n);
  for(size_t k=0;k<levels.size();++k) {
    for(unsigned j=0;j<sps;++j)base[margin+k*sps+j]=levels[k]?1.0:-1.0;
  }
  const int half=50;
  double taps[2*half+1]{};
  double norm=0;
  const double sigma=std::sqrt(std::log(2.0))/(2.0*pi*0.4);
  for(int k=-half;k<=half;++k) {
    const double t=double(k)/sps;
    taps[k+half]=std::exp(-0.5*t*t/(sigma*sigma));
    norm+=taps[k+half];
  }
  std::vector<std::pair<float,float>> samples(n);
  double phase=0;
  const double carrier=(channel_index==0?-25000:25000)+cfo_hz;
  for(size_t k=0;k<n;++k) {
    double shaped=0;
    for(int j=-half;j<=half;++j)
      if(static_cast<int64_t>(k)+j>=0 && k+j<n)
        shaped+=base[k+j]*taps[j+half];
    shaped/=norm;
    phase+=2*pi*(carrier+2400*shaped)/rx::kInputRateHz;
    samples[k]={static_cast<float>(0.65*std::cos(phase)),static_cast<float>(0.65*std::sin(phase))};
  }
  return samples;
}
std::vector<uint8_t> cu8(const std::vector<std::pair<float,float>>& samples) {
  std::vector<uint8_t> out;
  out.reserve(samples.size()*2);
  for(const auto& s:samples) {
    const auto cv=[](float v) {
      return static_cast<uint8_t>(std::max(0,std::min(255,static_cast<int>(std::lround(127.5f+128.f*v)))));
    };
    out.push_back(cv(s.first));out.push_back(cv(s.second));
  }
  return out;
}
void send_chunks(rx::Receiver& receiver,const std::vector<uint8_t>& samples,Results& result,size_t step) {
  for(size_t n=0;n<samples.size();) {
    const size_t len=std::min(step & ~size_t{1},samples.size()-n);
    assert(receiver.process_cu8(samples.data()+n,len,rx::kInputRateHz,collect,&result));
    n+=len;
  }
}
void verify_rf_channel(unsigned channel,double cfo_hz,bool inverted,size_t step) {
  const auto data=make_type1(366967101+channel);
  const auto levels=nrzi_levels(wire_bits(data),inverted);
  const auto iq=cu8(gmsk(levels,channel,cfo_hz));
  rx::Receiver receiver;
  Results result;
  send_chunks(receiver,iq,result,step);
  const auto stats=receiver.stats();
  std::printf("CH%u cfo=%+.0f inv=%u samples=%llu packets=%zu crc=%llu/%llu bad=%llu\n",
     channel+1,cfo_hz,unsigned(inverted),static_cast<unsigned long long>(stats.iq_samples),result.packets.size(),
     static_cast<unsigned long long>(stats.channels[channel].crc_ok),
     static_cast<unsigned long long>(stats.channels[channel].candidate_frames),
     static_cast<unsigned long long>(stats.channels[channel].crc_failed));
  assert(result.packets.size()==1);
  assert(result.packets[0].channel==channel+1);
  assert(result.packets[0].bit_count==168);
  assert(std::memcmp(result.packets[0].bytes,data.data(),data.size())==0);
}
void verify_both_channels() {
  const auto a=make_type1(101000001),b=make_type1(202000002);
  const auto ia=gmsk(nrzi_levels(wire_bits(a)),0);
  const auto ib=gmsk(nrzi_levels(wire_bits(b)),1);
  const size_t n=std::max(ia.size(),ib.size());
  std::vector<std::pair<float,float>> together(n);
  for(size_t i=0;i<n;++i) {
    if(i<ia.size()){together[i].first+=ia[i].first*0.6f;together[i].second+=ia[i].second*0.6f;}
    if(i<ib.size()){together[i].first+=ib[i].first*0.6f;together[i].second+=ib[i].second*0.6f;}
  }
  rx::Receiver receiver;Results out;
  send_chunks(receiver,cu8(together),out,3276);
  const auto stats=receiver.stats();
  std::printf("BOTH packets=%zu CH1=%llu CH2=%llu\n",out.packets.size(),
    static_cast<unsigned long long>(stats.channels[0].crc_ok),
    static_cast<unsigned long long>(stats.channels[1].crc_ok));
  assert(out.packets.size()==2);
  assert(out.packets[0].channel!=out.packets[1].channel);
}
void verify_pipeline_integration() {
  const auto payload=make_type1(302003001);
  const auto samples=cu8(gmsk(nrzi_levels(wire_bits(payload)),0));
  static rx::Pipeline pipeline;
  size_t off=0;
  uint64_t clock_ms=1000;
  while(off<samples.size()) {
    const size_t n=std::min<size_t>(2400,samples.size()-off);
    assert(pipeline.offer(samples.data()+off,n,rx::kInputRateHz,
                          rx::kCenterHz,42,clock_ms));
    off+=n;
    clock_ms+=5;
  }
  const auto summary=pipeline.stats();
  assert(summary.packets==1 && summary.messages_decoded==1 && summary.station_updates==1);
  assert(summary.parse_rejected==0 && summary.input_refused==0);
  assert(summary.rf.channels[0].crc_ok==1);
  const auto* station=pipeline.stations().find(302003001);
  assert(station && station->has_position);
  assert(station->lon_e7==-1223000000 && station->lat_e7==474000000);
  assert(station->history_size==1);
  assert(!pipeline.offer(samples.data(),2400,rx::kInputRateHz,
                         161975000,42,clock_ms));
  assert(pipeline.stats().input_refused==1);
  // New owner generation is explicit and must cut stale IQ framing state.
  const uint64_t old_samples=pipeline.stats().rf.iq_samples;
  assert(pipeline.offer(samples.data(),2400,rx::kInputRateHz,
                        rx::kCenterHz,43,clock_ms));
  assert(pipeline.stats().generation_changes==2);
  assert(pipeline.stats().rf.iq_samples==old_samples+1200);
  // A large time gap also rejects partial-frame continuity across blocks.
  assert(pipeline.offer(samples.data(),2400,rx::kInputRateHz,
                        rx::kCenterHz,43,clock_ms+500));
  assert(pipeline.stats().iq_gaps==1);
  assert(pipeline.stations().size()==1);
}
void verify_noise_and_maximum_frame() {
  rx::Hdlc h;
  Results decoded;
  std::vector<uint8_t> maximal(rx::kMaxPayloadBytes,0xff);
  maximal[0]=0x01; // valid payload for framing purposes; no parser is involved.
  for(int bit:nrzi_levels(wire_bits(maximal)))h.process_level(bit,1,collect,&decoded);
  assert(decoded.packets.size()==1);
  assert(decoded.packets[0].bit_count==rx::kMaxPayloadBits);
  assert(std::memcmp(decoded.packets[0].bytes,maximal.data(),maximal.size())==0);
  // Unstructured RF noise must not be interpreted as valid vessel traffic.
  std::vector<uint8_t> noise(2*rx::kInputRateHz/2);
  uint32_t seed=0x12345a2bu;
  for(auto& v:noise) {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;v=seed&0xff;}
  static rx::Receiver rx_noise;
  Results result;
  send_chunks(rx_noise,noise,result,4096);
  assert(result.packets.empty());
  assert(rx_noise.stats().channels[0].crc_ok==0 && rx_noise.stats().channels[1].crc_ok==0);
}
void verify_discontinuity_and_input_guards() {
  const auto msg=make_type1(303003001);
  const auto samples=cu8(gmsk(nrzi_levels(wire_bits(msg)),0));
  static rx::Receiver receiver;
  Results result;
  const size_t split=2000;
  assert(receiver.process_cu8(samples.data(),split,rx::kInputRateHz,collect,&result));
  receiver.discontinuity(); // a lost/retuned IQ stream must not join two partial HDLC frames
  assert(receiver.process_cu8(samples.data()+split,samples.size()-split,
                              rx::kInputRateHz,collect,&result));
  assert(result.packets.empty());
  assert(receiver.stats().iq_samples==samples.size()/2);
  assert(!receiver.process_cu8(samples.data(),3,rx::kInputRateHz,collect,&result));
  assert(!receiver.process_cu8(samples.data(),2,960000,collect,&result));
  assert(receiver.stats().bad_blocks==2);
  receiver.reset();
  result.packets.clear();
  send_chunks(receiver,samples,result,2048);
  assert(result.packets.size()==1);
}
}
int main() {
  verify_direct_hdlc();
  verify_rf_channel(0,0,false,1700);
  verify_rf_channel(1,0,false,5130);
  verify_rf_channel(0,130,true,4096);
  verify_rf_channel(1,-140,true,998);
  verify_rf_channel(0,800,false,2048);
  verify_rf_channel(1,-800,false,4096);
  verify_both_channels();
  verify_pipeline_integration();
  verify_noise_and_maximum_frame();
  verify_discontinuity_and_input_guards();
  rx::Receiver receiver;Results out;
  assert(!receiver.process_cu8(nullptr,2,rx::kInputRateHz,collect,&out));
  assert(!receiver.process_cu8(nullptr,0,1234,collect,&out));
  assert(receiver.stats().bad_blocks==2);
  std::puts("AIS dual-channel GMSK/HDLC/FCS host IQ tests passed (synthetic only)");
}

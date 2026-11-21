#include "ft8_receiver.hpp"
#include "ft8_codec.hpp"
#include "ft8_ldpc.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

// Test-only clean waveform, using the shared protocol profile and the same
// payload construction as tests/ft8_pipeline_tests.cpp. Never sent to hardware.
static void decode_known_message(orc::zero::Ft8Receiver& receiver){
  using namespace orcsdr::ft8;
  const auto& profile=orcsdr::ftx::profile(orcsdr::ftx::Mode::ft4);
  codec::PayloadBits payload{};
  const auto put=[&](size_t offset,size_t bits,uint32_t value){
    for(size_t n=0;n<bits;++n)payload[offset+n]=uint8_t((value>>(bits-1-n))&1);
  };
  uint32_t call=0;assert(codec::encode_standard_callsign("K1ABC",&call));
  put(0,28,2);put(29,28,call);put(59,15,5*1800+13*100+42);put(74,3,1);
  codec::restore_ft4_payload(&payload);
  const auto codeword=ldpc::encode(codec::append_crc(payload));
  std::vector<uint8_t> tones(profile.channel_symbols,0);
  for(size_t n=0;n<profile.sync_block_count;++n){
    const auto& block=profile.sync[n];
    for(size_t k=0;k<block.length;++k)tones[block.first_symbol+k]=block.tones[k];
  }
  size_t bit=0;
  for(size_t n=0;n<profile.data_block_count;++n){
    const auto& block=profile.data[n];
    for(size_t k=0;k<block.length;++k){
      uint8_t label=0;
      for(unsigned b=0;b<profile.bits_per_tone;++b)label=uint8_t((label<<1)|codeword[bit++]);
      for(unsigned tone=0;tone<profile.tone_count;++tone)
        if(profile.tone_bits[tone]==label)tones[block.first_symbol+k]=uint8_t(tone);
    }
  }
  assert(bit==174&&receiver.begin(orcsdr::ftx::Mode::ft4));
  orc::linux_sdr::IqBlock iq;iq.meta={7047500,240000,2,0,0,0,false};iq.cu8.resize(4800);
  double phase=0;unsigned found=0,slots=0;
  for(unsigned n=0;n<750;++n){
    iq.meta.first_sample_index=uint64_t(n)*2400;
    for(size_t k=0;k<2400;++k){
      const uint64_t sample=iq.meta.first_sample_index+k;
      const int64_t audio_sample=int64_t(sample/20)-6000; // frame starts 0.5 s into the slot
      const int64_t symbol=audio_sample>=0?audio_sample/profile.symbol_samples:-1;
      if(symbol<0||size_t(symbol)>=tones.size()){
        iq.cu8[k*2]=iq.cu8[k*2+1]=128;continue;
      }
      const double hz=1000+tones[size_t(symbol)]*(12000./profile.symbol_samples);
      phase=std::fmod(phase+6.283185307179586*hz/240000,6.283185307179586);
      iq.cu8[k*2]=uint8_t(std::lround(127.5+60*std::cos(phase)));
      iq.cu8[k*2+1]=uint8_t(std::lround(127.5+60*std::sin(phase)));
    }
    assert(receiver.consume(iq,7047500,true,30000000000ull+n*10000000ull,
      [&](uint64_t epoch,const Decode* rows,size_t count){
        assert(epoch==30000);++slots;
        for(size_t k=0;k<count;++k)
          if(!std::strcmp(rows[k].message,"CQ K1ABC FN42")){
            assert(rows[k].mode==DigitalMode::ft4);assert(!std::strcmp(rows[k].grid,"FN42"));++found;
          }
      }));
  }
  assert(slots==1&&found==1);
  std::cout<<"Synthetic FT4 IQ decoded CQ K1ABC FN42 through the Zero receiver PASS\n";
}

int main(){
  orc::zero::Ft8Receiver receiver;
  assert(receiver.begin(orcsdr::ftx::Mode::ft4));
  orc::linux_sdr::IqBlock block;
  block.meta={7047500,240000,1,0,0,0,false};
  block.cu8.assign(4800,128);
  unsigned slots=0;
  for(unsigned n=0;n<1500;++n){
    block.meta.first_sample_index=uint64_t(n)*2400;
    assert(receiver.consume(block,7047500,true,30000000000ull+n*10000000ull,
      [&](uint64_t utc,const orcsdr::ft8::Decode*,size_t count){assert(utc==30000||utc==37500);assert(count==0);++slots;}));
  }
  assert(slots==2);
  decode_known_message(receiver);
  assert(receiver.begin()); // FT8 remains the default, with a new partial slot.
  std::cout<<"Native FT4 receiver completed two silent slots with no false messages PASS\n";
}

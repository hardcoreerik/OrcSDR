#pragma once
#include "iq_queue.hpp"
#include "shortwave_ssb.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace orc::zero {
class SidebandPcm {
 public:
  bool consume(const linux_sdr::IqBlock& block,uint32_t station,
      orcsdr::shortwave_ssb::Mode mode,bool direct_q,int clarifier,
      std::vector<int16_t>& pcm) {
    using namespace orcsdr::shortwave_ssb;
    pcm.clear();
    const auto& meta=block.meta;
    const int64_t offset=int64_t(station)-meta.frequency_hz;
    if(meta.sample_rate_sps!=960000||!meta.receiver_generation||
       !station||!meta.frequency_hz||mode==Mode::am||
       offset<-12000||offset>12000||block.cu8.empty()||
       block.cu8.size()>32768||(block.cu8.size()&1)){
      reset();
      return false;
    }
    const auto sideband=demod_sideband(mode,meta.frequency_hz,direct_q);
    clarifier=clamp_clarifier(clarifier);
    if(meta.discontinuity||meta.dropped_samples_before||meta.receiver_generation!=generation_||
       meta.first_sample_index!=next_||meta.frequency_hz!=center_||station!=station_||
       clarifier!=clarifier_||direct_q!=direct_q_)
      reset();
    generation_=meta.receiver_generation;
    next_=meta.first_sample_index+block.cu8.size()/2;
    center_=meta.frequency_hz;
    station_=station;
    clarifier_=clarifier;
    direct_q_=direct_q;
    const auto carrier=direct_q&&spectrum_inverted(center_)?-offset:offset;
    process(demod_,block.cu8.data(),block.cu8.size(),4,sideband,clarifier,960000.f,
      [&](float value){
        pcm.push_back(static_cast<int16_t>(std::lround(std::clamp(value*128.f,-32768.f,32767.f))));
      },nullptr,int32_t(carrier));
    return true;
  }
  void reset(){
    demod_={};
    generation_=0;
  }
 private:
  orcsdr::shortwave_ssb::Demod demod_{};
  uint64_t generation_=0,next_=0;
  uint32_t center_=0,station_=0;
  int clarifier_=0;
  bool direct_q_=false;
};
}

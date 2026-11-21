#include "fm_dsp.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr double kPi=3.14159265358979323846;
constexpr unsigned kAudioRate=48000;
std::vector<uint8_t> synthetic_wfm(unsigned seconds,unsigned kIqRate,bool centered,bool narrow=false) {
  std::vector<uint8_t> iq(static_cast<size_t>(kIqRate)*seconds*2);
  double rf_phase=0.0;
  for(size_t i=0;i<iq.size()/2;++i) {
    // 1 kHz monophonic audio, 35 kHz peak FM deviation, station centered.
    const double audio=std::sin(2*kPi*1000.0*i/kIqRate);
    rf_phase+=2*kPi*((narrow?2500.0:35000.0)*audio+(centered||kIqRate==300000?0:kIqRate/4))/kIqRate;
    if(rf_phase>2*kPi)rf_phase-=2*kPi;
    iq[2*i]=static_cast<uint8_t>(std::clamp(std::lround(127.5+80*std::cos(rf_phase)),0l,255l));
    iq[2*i+1]=static_cast<uint8_t>(std::clamp(std::lround(127.5+80*std::sin(rf_phase)),0l,255l));
  }
  return iq;
}
std::vector<int16_t> decode(const std::vector<uint8_t>& iq,size_t chunk,unsigned kIqRate,bool centered,bool narrow=false) {
  orcnode_fm_dsp_t state{};
  if((narrow?orcnode_nfm_dsp_init(&state):orcnode_fm_dsp_init(&state,kIqRate,75))!=0)throw std::runtime_error("FM init");
  if(centered)state.mix_fs4=false;
  std::vector<int16_t> result;
  std::vector<int16_t> temporary(2048);
  for(size_t at=0;at<iq.size();) {
    const size_t bytes=std::min(chunk,iq.size()-at);
    const size_t produced=orcnode_fm_dsp_process(&state,iq.data()+at,bytes,
                                                  temporary.data(),temporary.size());
    if(produced>temporary.size())throw std::runtime_error("overrun");
    result.insert(result.end(),temporary.begin(),temporary.begin()+produced);
    at+=bytes;
  }
  return result;
}
}
int main() {
 for(bool narrow:{false,true})for(unsigned rate:{300000u,2560000u})for(bool centered:{false,true}){
  if(narrow&&(rate!=300000u||!centered))continue;
  const auto iq=synthetic_wfm(1,rate,centered,narrow);
  const auto a=decode(iq,4096,rate,centered,narrow),b=decode(iq,16384,rate,centered,narrow);
  if(a.size()<47995||a.size()>48005||a!=b) {
    std::cerr<<"48k output and chunk independence failed: "<<a.size()<<" / "<<b.size()<<"\n";
    return 1;
  }
  const size_t skip=4800; // 100 ms settling
  double sq=0;int crossings=0;
  for(size_t i=skip+1;i<a.size();++i) {
    if(a[i-1]<0&&a[i]>=0)++crossings;
    sq+=double(a[i])*a[i];
  }
  const double rms=std::sqrt(sq/(a.size()-skip));
  const double hz=double(crossings)*kAudioRate/(a.size()-skip);
  if(hz<950||hz>1050||rms<200||rms>14000) {
    std::cerr<<"FM tone recovery failed: hz="<<hz<<" rms="<<rms<<"\n";
    return 2;
  }
  std::cout<<"rate="<<rate<<" narrow="<<narrow<<" original FM DSP recovered 1 kHz test: observed_hz="<<hz
           <<" rms_pcm="<<rms<<" samples="<<a.size()
           <<" chunk_4096_vs_16384=identical\n";
 }
}

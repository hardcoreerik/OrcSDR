#include "airband.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace orc::zero::airband;
namespace {
constexpr double pi=3.14159265358979323846;
struct Capture { std::vector<int16_t> pcm; };
void save_pcm(void* v,const int16_t* p,size_t n){
  auto* cap=static_cast<Capture*>(v);cap->pcm.insert(cap->pcm.end(),p,p+n);
}
struct Generator {
  uint32_t rate=2560000;
  uint64_t index=0;
  float c=1,s=0;
  float stepc=1,steps=0;
  explicit Generator(uint32_t r,uint32_t desired,uint32_t center):rate(r){
    const double theta=2*pi*(double(int64_t(desired)-int64_t(center)))/r;
    stepc=float(std::cos(theta));steps=float(std::sin(theta));
  }
  void fill(std::vector<uint8_t>& buf){
    for(size_t i=0;i<buf.size()/2;++i){
      const double t=double(index++)/rate;
      const float env=0.40f*(1.0f+0.56f*float(std::sin(2*pi*1000*t)));
      buf[2*i]=uint8_t(std::clamp(std::lround(127.5+128*env*c),0l,255l));
      buf[2*i+1]=uint8_t(std::clamp(std::lround(127.5+128*env*s),0l,255l));
      const float nc=c*stepc-s*steps;s=c*steps+s*stepc;c=nc;
      if((index&4095u)==0){const float r=std::sqrt(c*c+s*s);c/=r;s/=r;}
    }
  }
};
}
int main(int argc,char** argv){
  std::string screenshot,pcm_path;bool demo=false;
  for(int i=1;i<argc;++i){const std::string a=argv[i];
    if(a=="--demo")demo=true;
    else if(a=="--screenshot"&&i+1<argc)screenshot=argv[++i];
    else if(a=="--pcm"&&i+1<argc)pcm_path=argv[++i];
    else {std::cerr<<"Use --demo --screenshot <path.ppm> [--pcm <path.s16le>]\n";return 2;}
  }
  if(!demo||screenshot.empty()){std::cerr<<"Explicit --demo and --screenshot required\n";return 2;}
  Controller rx;
  rx.activate(kGuardHz);rx.set_demo(true);rx.audio_available(true);
  for(int i=0;i<6;++i)rx.action(Action::squelch_down); // labeled no-squelch diagnostic mode
  const Request req=rx.requested_radio();rx.radio_applied(req.rf_center_hz,req.sample_rate_sps,1);
  Generator gen(req.sample_rate_sps,kGuardHz,req.rf_center_hz);Capture capture;
  std::vector<uint8_t> iq(16384);
  for(int b=0;b<160;++b){
    const uint64_t start=gen.index;gen.fill(iq);
    orc::linux_sdr::IqMetadata meta{};meta.frequency_hz=req.rf_center_hz;
    meta.sample_rate_sps=req.sample_rate_sps;meta.receiver_generation=1;
    meta.first_sample_index=start;
    if(!rx.consume(iq.data(),iq.size(),meta,save_pcm,&capture))return 3;
  }
  Renderer render;render.render(rx.snapshot());
  if(!render.save_ppm(screenshot)){std::cerr<<"Cannot write screenshot\n";return 4;}
  if(!pcm_path.empty()){
    std::ofstream out(pcm_path,std::ios::binary);if(!out)return 5;
    out.write(reinterpret_cast<const char*>(capture.pcm.data()),static_cast<std::streamsize>(capture.pcm.size()*sizeof(int16_t)));
    if(!out)return 6;
  }
  std::cout<<"DEMO synthetic AM preview: "<<screenshot<<" (320x170), "<<capture.pcm.size()<<" PCM frames at 48000 Hz\n";
  return 0;
}

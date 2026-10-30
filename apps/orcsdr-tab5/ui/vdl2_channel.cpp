// SPDX-License-Identifier: AGPL-3.0-only
#include "vdl2_channel.hpp"
#include <cmath>
namespace orcsdr::vdl2 {
namespace { constexpr double kPi=3.1415926535897932384626; }
Channel::Channel(){configure(105000,105000);}
bool Channel::configure(uint32_t input,uint32_t channel,int32_t offset){
  if(!(channel==50000 || channel==105000))return false;
  if(input!=channel && !((input==250000 && channel==50000) ||
                          (input==1050000 && channel==105000)))return false;
  if(std::abs(int64_t(offset))>int64_t(input)/2-15000)return false;
  input_rate_=input;offset_=offset;
  decimation_=input/channel;phase_=0;delay_index_=0;
  for(auto& x:delay_)x={};
  const double ang=-2*kPi*offset/double(input);
  step_i_=float(std::cos(ang));step_q_=float(std::sin(ang));
  mix_i_=1;mix_q_=0;mixer_ticks_=0;
  // Hamming-windowed sinc, cutoff 10.5 kHz (before RC band edge at 8.4k).
  // Output sample rate Nyquist 25 / 52.5 kHz; 127-tap FIR precomputed once.
  double sum=0;
  for(size_t n=0;n<kTaps;++n){
    const double x=double(n)-double(kTaps-1)/2.;
    const double fc=10500.0/double(input);
    const double sinc=x==0.?2*fc:std::sin(2*kPi*fc*x)/(kPi*x);
    const double window=0.54-0.46*std::cos(2*kPi*n/double(kTaps-1));
    taps_[n]=float(sinc*window);sum+=taps_[n];
  }
  for(auto& v:taps_)v=float(double(v)/sum);
  return rx_.set_rate(channel);
}
void Channel::process_complex(const ComplexSample* samples,size_t count){
  if(!samples)return;
  for(size_t n=0;n<count;++n){
    const ComplexSample in=samples[n];
    const ComplexSample mixed{in.i*mix_i_-in.q*mix_q_,in.i*mix_q_+in.q*mix_i_};
    if(offset_){
      const float i=mix_i_*step_i_-mix_q_*step_q_;
      const float q=mix_i_*step_q_+mix_q_*step_i_;
      mix_i_=i;mix_q_=q;
      if((++mixer_ticks_&4095u)==0){
        const float m=std::sqrt(mix_i_*mix_i_+mix_q_*mix_q_);
        if(m>0.01f){mix_i_/=m;mix_q_/=m;}
      }
    }
    if(decimation_==1u){rx_.process_sample(mixed.i,mixed.q);continue;}
    delay_[delay_index_]=mixed;
    delay_index_=(delay_index_+1)%kTaps;
    if(++phase_<decimation_)continue;
    phase_=0;
    float i=0,q=0;
    size_t index=delay_index_;
    for(size_t j=0;j<kTaps;++j){
      if(index==0)index=kTaps;
      const auto& c=delay_[--index];
      i+=c.i*taps_[j];q+=c.q*taps_[j];
    }
    rx_.process_sample(i,q);
  }
}
void Channel::process_cu8(const uint8_t* input,size_t count){
  if(!input)return;
  // Keep a bounded host/device conversion stack. A 256-sample chunk is enough.
  ComplexSample chunk[256];
  for(size_t first=0;first<count;first+=256){
    const size_t n=first+256<=count?256:count-first;
    for(size_t j=0;j<n;++j){
      chunk[j]={(float(input[2*(first+j)])-127.5f)/127.5f,
                (float(input[2*(first+j)+1])-127.5f)/127.5f};
    }
    process_complex(chunk,n);
  }
}
} // namespace orcsdr::vdl2

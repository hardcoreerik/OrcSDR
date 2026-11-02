#include "hfdl_decoder.hpp"
#include <cmath>

namespace orcsdr::hfdl {
namespace { constexpr float kPi=3.14159265358979323846f; }
Decoder::Decoder(){set_config(cfg_);}
void Decoder::reset(){
  stats_=FrontendStats{};history_pos_=0;decim_phase_=0;
  oscillator_i_=1;oscillator_q_=0;
  for(int i=0;i<kTaps;++i){hist_i_[i]=0;hist_q_[i]=0;}
}
void Decoder::configure_filter(){
  // Windowed-sinc LPF. Configure before stream, never in sample callback.
  const float cutoff=1550.0f/static_cast<float>(cfg_.sample_rate);
  float sum=0;
  for(int i=0;i<kTaps;++i){
    const int n=i-(kTaps-1)/2;
    const float tap=n==0?2*cutoff:
      std::sin(2*kPi*cutoff*n)/(kPi*n);
    const float window=0.54f-0.46f*std::cos(2*kPi*i/(kTaps-1));
    coeff_[i]=tap*window;sum+=coeff_[i];
  }
  for(int i=0;i<kTaps;++i)coeff_[i]/=sum;
  const float theta=-2*kPi*static_cast<float>(kCarrierOffsetHz)/
                    static_cast<float>(cfg_.sample_rate);
  step_i_=std::cos(theta);step_q_=std::sin(theta);
}
void Decoder::set_config(const Config& cfg){
  cfg_=cfg;
  valid_config_=cfg.sample_rate>=kOutputRate &&
                cfg.sample_rate<=192000 &&
                cfg.sample_rate%kOutputRate==0;
  reset();
  if(!valid_config_)return;
  decim_step_=static_cast<int>(cfg.sample_rate/kOutputRate);
  configure_filter();
}
void Decoder::set_model(hfdl_ui::Model* model){model_=model;}
void Decoder::consume(float i,float q){
  const float mixed_i=i*oscillator_i_-q*oscillator_q_;
  const float mixed_q=i*oscillator_q_+q*oscillator_i_;
  hist_i_[history_pos_]=mixed_i;hist_q_[history_pos_]=mixed_q;
  if(++history_pos_==kTaps)history_pos_=0;
  if(++decim_phase_>=decim_step_){
    decim_phase_=0;
    float out_i=0,out_q=0;
    int idx=history_pos_;
    for(int k=0;k<kTaps;++k){
      if(idx==0)idx=kTaps;
      --idx;out_i+=coeff_[k]*hist_i_[idx];out_q+=coeff_[k]*hist_q_[idx];
    }
    const float power=out_i*out_i+out_q*out_q;
    stats_.channel_power+=0.001f*(power-stats_.channel_power);
    ++stats_.channel_samples;
  }
  const float old_i=oscillator_i_;
  oscillator_i_=old_i*step_i_-oscillator_q_*step_q_;
  oscillator_q_=old_i*step_q_+oscillator_q_*step_i_;
  if((++stats_.input_samples & 4095u)==0){
    const float scale=1.0f/std::sqrt(oscillator_i_*oscillator_i_+
                                     oscillator_q_*oscillator_q_);
    oscillator_i_*=scale;oscillator_q_*=scale;
  }
}
bool Decoder::process(const float* i,const float* q,size_t count){
  if(!valid_config_||!i){++stats_.invalid_blocks;return false;}
  for(size_t n=0;n<count;++n)consume(i[n],q?q[n]:0);
  return false; // No sync, FEC, FCS, or message parser yet.
}
bool Decoder::process_audio(const float* audio,size_t count){
  return process(audio,nullptr,count);
}
} // namespace orcsdr::hfdl

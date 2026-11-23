// SPDX-License-Identifier: AGPL-3.0-only
#include "vdl2_receiver.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace orcsdr::vdl2 {
namespace {
constexpr float kPi=3.14159265358979323846f;
constexpr float kQuarterPi=kPi/4.f;
constexpr float kRate=10500.f;
constexpr uint8_t kUw[16]={0,3,2,4,0,1,6,4,1,7,2,5,6,5,7,3};
constexpr uint8_t kInverseGray[8]={0,4,6,2,3,7,5,1};
ComplexSample diff(ComplexSample now,ComplexSample prev) {
 return {now.i*prev.i+now.q*prev.q, now.q*prev.i-now.i*prev.q};
}
float square(ComplexSample c) { return c.i*c.i+c.q*c.q; }
}
Receiver::Receiver(uint32_t rate) {
  for(unsigned i=0;i<16;++i){
    ref_phase_[i]=i?ref_phase_[i-1]+kUw[i]*kQuarterPi:0.f;
    expected_[i]={std::cos(kUw[i]*kQuarterPi),-std::sin(kUw[i]*kQuarterPi)};
  }
  set_rate(rate);
}
bool Receiver::set_rate(uint32_t rate){
  if(rate!=50000 && rate!=105000)return false;
  channel_rate_=rate;
  symbol_step_=double(rate)/10500.;
  reset();
  return true;
}
void Receiver::reset(){
  sample_count_=0;cursor_=0;next_symbol_=0;uw_start_=0;
  noise_=1e-6f;theta_=initial_theta_=0;stage_=Stage::hunt;
  received_bits_=needed_bits_=0;descrambler_.reset();frames_.reset();
}
ComplexSample Receiver::sample(double abs)const {
  if(abs<0 || abs<double(sample_count_>kRingSize?sample_count_-kRingSize:0))return {};
  const size_t n=size_t(abs);
  if(n+1>=sample_count_)return {};
  const float x=float(abs-double(n));
  const ComplexSample a=ring_[n%kRingSize],b=ring_[(n+1)%kRingSize];
  return {a.i+(b.i-a.i)*x,a.q+(b.q-a.q)*x};
}
bool Receiver::correlate(double pos,float& metric){
  float ri=0,rq=0,norm=0,minimum=1e30f;
  ComplexSample prev=sample(pos);
  for(unsigned j=1;j<16;++j){
    ComplexSample v=sample(pos+j*symbol_step_);
    ComplexSample d=diff(v,prev);prev=v;
    const float cr=d.i*expected_[j].i-d.q*expected_[j].q;
    const float cq=d.i*expected_[j].q+d.q*expected_[j].i;
    ri+=cr;rq+=cq;
    const float mag=std::hypot(d.i,d.q);
    norm+=mag;minimum=std::min(minimum,mag);
  }
  metric=norm>1e-9f && minimum>=0.01f*norm/15.f ? std::hypot(ri,rq)/norm:0.f;
  return metric>0.6f;
}
bool Receiver::refine(double pos,double& best_pos,float& theta,float& cost){
  float best=1e30f;
  const float half=std::max<float>(3.f,float(symbol_step_*0.63));
  for(float offset=-half;offset<=half;offset+=0.25f){
    const double p=pos+offset;
    if(p<0 || p<double(sample_count_>kRingSize?sample_count_-kRingSize:0))continue;
    float r[16],w[16];
    for(unsigned j=0;j<16;++j){
      const ComplexSample v=sample(p+j*symbol_step_);
      r[j]=std::atan2(v.q,v.i)-ref_phase_[j];
      w[j]=square(v);
    }
    for(unsigned j=1;j<16;++j){
      float d=r[j]-r[j-1];
      while(d>kPi){r[j]-=2*kPi;d=r[j]-r[j-1];}
      while(d<-kPi){r[j]+=2*kPi;d=r[j]-r[j-1];}
    }
    float sw=0,sk=0,sr=0;
    for(unsigned j=0;j<16;++j){sw+=w[j];sk+=j*w[j];sr+=r[j]*w[j];}
    if(sw<1e-12f)continue;
    const float km=sk/sw,rm=sr/sw;
    float num=0,den=0;
    for(unsigned j=0;j<16;++j){
      const float dk=float(j)-km;
      num+=w[j]*dk*(r[j]-rm);den+=w[j]*dk*dk;
    }
    if(den<1e-12f)continue;
    const float b=num/den,a=rm-b*km;
    float c=0;
    for(unsigned j=0;j<16;++j){const float e=r[j]-a-b*float(j);c+=w[j]*e*e;}
    c/=sw;
    if(c<best){best=c;best_pos=p;theta=b;}
  }
  cost=best;
  return best<0.25f;
}
void Receiver::rewind_hunt(){
  stage_=Stage::hunt;received_bits_=needed_bits_=0;
  // No retrospective long-buffer scan on embedded streaming POC.
  cursor_=std::max(cursor_,double(sample_count_) - (19.*symbol_step_+4.));
}
bool Receiver::put_received_bit(uint8_t bit){
  if(received_bits_>=received_.size()*8u){++stats_.dropped;rewind_hunt();return false;}
  if((received_bits_&7u)==0)received_[received_bits_/8u]=0;
  if(bit)received_[received_bits_/8u]|=uint8_t(1u<<(received_bits_%8u));
  ++received_bits_;
  if(received_bits_==25u){
    uint32_t hdr=0;
    for(unsigned j=0;j<25;++j)hdr=(hdr<<1u)|uint32_t((received_[j/8u]>>(j%8u))&1u);
    BurstHeader parsed{};
    if(!decode_burst_header(hdr,parsed)||parsed.data_bits==0||parsed.data_bits>kMaxVdl2DataBits){
      ++stats_.header_fail;rewind_hunt();return false;
    }
    needed_bits_=25u+RsCodec::tx_bits_for(parsed.data_bits);
    if(needed_bits_>received_.size()*8u){++stats_.header_fail;rewind_hunt();return false;}
    ++stats_.header_ok;
  }
  if(needed_bits_ && received_bits_>=needed_bits_)finish_burst();
  return true;
}
void Receiver::finish_burst(){
  uint32_t header=0;
  for(unsigned j=0;j<25;++j)header=(header<<1u)|uint32_t((received_[j/8u]>>(j%8u))&1u);
  BurstHeader parsed{};
  if(!decode_burst_header(header,parsed)){++stats_.header_fail;rewind_hunt();return;}
  const size_t transmitted_bits=needed_bits_-25u;
  std::memset(transmitted_.data(),0,(transmitted_bits+7u)/8u);
  for(size_t j=0;j<transmitted_bits;++j)
    if((received_[(j+25u)/8u]>>((j+25u)%8u))&1u)
      transmitted_[j/8u]|=uint8_t(1u<<(j%8u));
  FecResult fec{};
  if(!rs_.deinterleave(transmitted_.data(),transmitted_bits,parsed.data_bits,
                       recovered_.data(),recovered_.size(),fec)){
    ++stats_.fec_fail;rewind_hunt();return;
  }
  ++stats_.fec_ok;
  frames_.reset();
  for(size_t j=0;j<fec.data_bits;++j)
    frames_.push_bit((recovered_[j/8u]>>(j%8u))&1u);
  ++stats_.bursts_completed;
  rewind_hunt();
}
void Receiver::collect_available(){
  while(stage_==Stage::collect && next_symbol_+1.0<double(sample_count_)){
    ComplexSample s=sample(next_symbol_);
    ComplexSample d=diff(s,previous_);
    previous_=s;next_symbol_+=symbol_step_;
    const float phase=std::atan2(d.q,d.i)-theta_;
    const float scaled=std::round(phase/kQuarterPi);
    const unsigned idx=unsigned((int(scaled)%8+8)%8);
    const float residual=phase-scaled*kQuarterPi;
    theta_+=0.1f*residual;
    const uint8_t triplet=kInverseGray[idx];
    for(unsigned bit=0;bit<3 && stage_==Stage::collect;++bit)
      put_received_bit(descrambler_.apply_bit(uint8_t((triplet>>bit)&1u)));
  }
}
void Receiver::process_sample(float i,float q){
  ring_[sample_count_%kRingSize]={i,q};++sample_count_;
  if(stage_==Stage::collect){collect_available();return;}
  // Allow complete 16-symbol correlation plus timing-refinement margin.
  const double end=double(sample_count_)-19.0*symbol_step_-4.0;
  while(cursor_<end){
    if(cursor_ < double(sample_count_>kRingSize?sample_count_-kRingSize:0))
      cursor_=double(sample_count_-kRingSize);
    const ComplexSample s=sample(cursor_);
    const float energy=square(s);
    const double test=cursor_;
    cursor_+=1.;
    if(energy<noise_*12.f){noise_+=0.0001f*(energy-noise_);continue;}
    noise_*=1.00000001f;
    float metric=0;
    if(!correlate(test,metric))continue;
    ++stats_.uw_candidates;
    double position=0;float carrier=0,cost=0;
    if(!refine(test,position,carrier,cost))continue;
    ++stats_.uw_locked;
    stats_.last_preamble_cost=cost;
    stats_.last_cfo_hz=carrier*kRate/(2*kPi);
    uw_start_=position;
    next_symbol_=position+16.0*symbol_step_;
    previous_=sample(position+15.0*symbol_step_);
    theta_=initial_theta_=carrier;
    received_bits_=needed_bits_=0;
    descrambler_.reset();stage_=Stage::collect;
    collect_available();
    return;
  }
}
void Receiver::process_cu8(const uint8_t* iq,size_t count){
  if(!iq)return;
  for(size_t n=0;n<count;++n)
    process_sample((float(iq[2*n])-127.5f)/127.5f,(float(iq[2*n+1])-127.5f)/127.5f);
}
} // namespace orcsdr::vdl2

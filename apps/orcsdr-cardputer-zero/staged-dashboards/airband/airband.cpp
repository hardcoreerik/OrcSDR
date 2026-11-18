#include "airband.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <limits>

namespace orc::zero::airband {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr float inv_u8=1.0f/128.0f;
constexpr float inv_cic_quant=1.0f/4096.0f;
constexpr float hp_alpha=0.9678f; // 250 Hz at 48 kHz
constexpr float lp_alpha=0.375f; // approximately 3.6 kHz at 48 kHz, cascaded
void safe_error(std::array<char,64>& dest,const char* text){
  std::snprintf(dest.data(),dest.size(),"%.63s",text?text:"");
}
uint64_t add_signed(uint64_t a,int64_t b){return a+static_cast<uint64_t>(b);} // modulo 2^64
int64_t signed_mod(uint64_t v){return v<=uint64_t(INT64_MAX)?int64_t(v):(-int64_t(~v)-1);}
}
bool valid_frequency(uint32_t hz){return hz>=kMinHz&&hz<=kMaxHz;}
uint32_t step_frequency(uint32_t hz,int direction){
  if(!valid_frequency(hz)||!direction)return hz;
  const int64_t n=static_cast<int64_t>(hz)+ (direction>0?int64_t(kStepHz):-int64_t(kStepHz));
  return static_cast<uint32_t>(std::clamp<int64_t>(n,kMinHz,kMaxHz));
}
int32_t display_offset_hz(uint32_t selected,uint32_t center){return int32_t(int64_t(selected)-int64_t(center));}
bool parse_frequency_mhz(const std::string& s,uint32_t& hz){
  if(s.size()<3||s.size()>11)return false;
  uint64_t mhz=0,frac=0,pad=100000;bool dot=false,has_digit=false;
  for(char c:s){
    if(c=='.'){if(dot||!has_digit)return false;dot=true;continue;}
    if(c<'0'||c>'9')return false;
    if(!dot){mhz=mhz*10+uint64_t(c-'0');if(mhz>999)return false;has_digit=true;}
    else {if(!pad)return false;frac+=pad*uint64_t(c-'0');pad/=10;}
  }
  const uint64_t value=mhz*1000000ULL+frac;
  if(!has_digit||value>UINT32_MAX||!valid_frequency(static_cast<uint32_t>(value)))return false;
  hz=static_cast<uint32_t>(value);return true;
}
Controller::Controller(){
  for(int phase=0;phase<3;++phase){
    const double frac=phase/3.0;
    double total=0;
    for(int j=0;j<97;++j){
      const double v=j-48.0+frac;
      const double cutoff=5200.0/80000.0;
      const double sinc=std::abs(v)<1e-9?2*cutoff:std::sin(2*pi*cutoff*v)/(pi*v);
      const double window=0.42-0.5*std::cos(2*pi*j/96.0)+0.08*std::cos(4*pi*j/96.0);
      polyphase_[phase][j]=static_cast<float>(sinc*window);total+=sinc*window;
    }
    for(auto& t:polyphase_[phase])t=static_cast<float>(t/total);
  }
}
void Controller::reset_squelch(){noise_floor_db_=-120;noise_ready_=false;squelch_samples_=0;squelch_hang_=0;level_=0;s_.squelch_open=false;s_.carrier_detected=false;s_.relative_db=0;s_.channel_level_dbfs=-120;}
void Controller::reset_dsp(){
  cic_i_={};cic_q_={};decim_phase_=0;nco_re_=1;nco_im_=0;nco_count_=0;
  ring_i_.fill(0);ring_q_.fill(0);ring_pos_=0;samples_80k_=0;next_output_tick_=0;
  dc_=hp_x_=hp_y_=lp_a_=lp_b_=0;level_pow_=0;pcm_size_=0;fft_delay_=0;
  reset_squelch();
}
bool Controller::activate(uint32_t selected){
  if(!valid_frequency(selected))return false;
  s_=Snapshot{};s_.selected_hz=selected;s_.connection=Connection::connecting;
  s_.tune_pending=true;s_.requested_center_hz=requested_radio().rf_center_hz;
  applied_selected_hz_=0;have_continuity_=false;reset_dsp();return true;
}
Settings Controller::settings() const {
  return {s_.volume,s_.squelch_db,s_.gain_tenth_db,s_.user_muted,s_.gain_auto};
}
bool Controller::restore_settings(const Settings& v){
  if(v.volume<0||v.volume>100||v.squelch_db<0||v.squelch_db>30||
     v.gain_tenth_db<0||v.gain_tenth_db>500)return false;
  s_.volume=v.volume;s_.squelch_db=v.squelch_db;
  s_.gain_tenth_db=v.gain_tenth_db;s_.gain_auto=v.gain_auto;
  s_.user_muted=v.user_muted;return true;
}
Request Controller::requested_radio()const{
  const uint32_t shift=(s_.selected_hz>=136800000)?125000u:0u;
  return {shift?s_.selected_hz-125000u:s_.selected_hz+125000u,kRatePreferred,true};
}
bool Controller::tune_direct(uint32_t hz){
  if(!valid_frequency(hz)||s_.connection==Connection::off)return false;
  if(hz==s_.selected_hz&&!s_.tune_pending)return true;
  s_.selected_hz=hz;s_.requested_center_hz=requested_radio().rf_center_hz;
  s_.tune_pending=true;s_.has_spectrum=false;s_.waterfall_rows=0;s_.error[0]=0;
  have_continuity_=false;reset_dsp();return true;
}
bool Controller::action(Action a,char digit){
  switch(a){
    case Action::tune_up:return tune_direct(step_frequency(s_.selected_hz,1));
    case Action::tune_down:return tune_direct(step_frequency(s_.selected_hz,-1));
    case Action::guard:return tune_direct(kGuardHz);
    case Action::frequency_entry:s_.entry_active=true;s_.entry.fill(0);return true;
    case Action::entry_digit:{
      if(!s_.entry_active||digit<'0'||digit>'9')return false;
      const size_t len=std::strlen(s_.entry.data());
      if(len>=10)return false;
      s_.entry[len]=digit;return true;
    }
    case Action::entry_point:{
      if(!s_.entry_active||std::strchr(s_.entry.data(),'.'))return false;
      const size_t len=std::strlen(s_.entry.data());if(len>=10)return false;
      s_.entry[len]='.';return true;
    }
    case Action::entry_backspace:{
      if(!s_.entry_active)return false;
      const size_t len=std::strlen(s_.entry.data());
      if(len)s_.entry[len-1]=0;
      return true;
    }
    case Action::entry_cancel:s_.entry_active=false;s_.entry.fill(0);return true;
    case Action::entry_commit:{
      if(!s_.entry_active)return false;
      uint32_t hz=0;
      if(!parse_frequency_mhz(s_.entry.data(),hz)){safe_error(s_.error,"INVALID AIRBAND FREQUENCY");return false;}
      s_.entry_active=false;s_.entry.fill(0);return tune_direct(hz);
    }
    case Action::volume_up:s_.volume=std::min(100,s_.volume+5);return true;
    case Action::volume_down:s_.volume=std::max(0,s_.volume-5);return true;
    case Action::mute:s_.user_muted=!s_.user_muted;return true;
    case Action::squelch_up:s_.squelch_db=std::min(30,s_.squelch_db+1);return true;
    case Action::squelch_down:s_.squelch_db=std::max(0,s_.squelch_db-1);return true;
    case Action::gain_auto_toggle:s_.gain_auto=!s_.gain_auto;return true;
    case Action::gain_up:s_.gain_tenth_db=std::min(500,s_.gain_tenth_db+10);s_.gain_auto=false;return true;
    case Action::gain_down:s_.gain_tenth_db=std::max(0,s_.gain_tenth_db-10);s_.gain_auto=false;return true;
    case Action::stop:stop();return true;
  }return false;
}
void Controller::radio_applied(uint32_t center,uint32_t rate,uint64_t generation){
  if(center!=s_.requested_center_hz ||(rate!=kRatePreferred&&rate!=kRateFallback)||!generation){
    radio_failed("RADIO CENTER/RATE READBACK MISMATCH");return;
  }
  s_.applied_center_hz=center;applied_selected_hz_=s_.selected_hz;s_.input_rate=rate;s_.generation=generation;
  s_.connection=Connection::ready;s_.tune_pending=false;s_.error[0]=0;
  decimate_=rate/80000;const double theta=-2*pi*display_offset_hz(s_.selected_hz,center)/rate;
  nco_step_re_=float(std::cos(theta));nco_step_im_=float(std::sin(theta));
  have_continuity_=false;reset_dsp();
}
void Controller::radio_failed(const char* error){
  safe_error(s_.error,error?error:"RADIO ERROR");
  s_.tune_pending=false;s_.has_spectrum=false;s_.waterfall_rows=0;
  // Reject the failed target; old RF center remains known but host must re-confirm
  // generation/readback before we resume consuming or emitting audio.
  if(applied_selected_hz_)s_.selected_hz=applied_selected_hz_;
  s_.requested_center_hz=s_.applied_center_hz;
  s_.connection=Connection::error;have_continuity_=false;reset_dsp();
}
void Controller::audio_available(bool ready,const char* failure){
  s_.audio_ready=ready;if(!ready&&failure)safe_error(s_.error,failure);
}
void Controller::disconnected(const char* reason){
  s_.connection=Connection::error;s_.audio_ready=false;s_.has_spectrum=false;
  s_.waterfall_rows=0;s_.tune_pending=false;safe_error(s_.error,reason?reason:"RECEIVER LOST");
  have_continuity_=false;reset_dsp();
}
void Controller::set_known_queue_drops(uint64_t drops){s_.known_queue_drops=drops;}
void Controller::set_gain_readback(bool a,int db10){s_.gain_auto=a;s_.gain_tenth_db=db10;reset_squelch();}
void Controller::set_demo(bool v){s_.demo=v;}
void Controller::stop(){
  s_.connection=Connection::off;s_.audio_ready=false;s_.tune_pending=false;
  s_.waterfall_rows=0;s_.has_spectrum=false;have_continuity_=false;reset_dsp();
}
void Controller::pcm_emit(int16_t value,PcmCallback cb,void* ctx){
  pcm_[pcm_size_++]=value;
  if(pcm_size_==pcm_.size()){if(cb)cb(ctx,pcm_.data(),pcm_size_);pcm_size_=0;}
}
void Controller::process_80k(float i,float q,PcmCallback cb,void* ctx){
  ring_i_[ring_pos_]=i;ring_q_[ring_pos_]=q;ring_pos_=(ring_pos_+1)%ring_i_.size();
  const uint64_t n=samples_80k_++;
  if(n<next_output_tick_/3+48)return;
  const int phase=int(next_output_tick_%3);
  const auto& taps=polyphase_[phase];float fi=0,fq=0;
  for(int j=0;j<97;++j){const unsigned p=(ring_pos_+ring_i_.size()-1u-unsigned(j))%ring_i_.size();fi+=ring_i_[p]*taps[j];fq+=ring_q_[p]*taps[j];}
  next_output_tick_+=5;
  const float env=std::sqrt(fi*fi+fq*fq);
  dc_+=0.0045f*(env-dc_);const float voice=env-dc_;
  hp_y_=hp_alpha*(hp_y_+voice-hp_x_);hp_x_=voice;
  lp_a_+=lp_alpha*(hp_y_-lp_a_);lp_b_+=lp_alpha*(lp_a_-lp_b_);
  level_+=0.001f*(env-level_);
  level_pow_+=0.001f*(env*env-level_pow_);
  if(++squelch_samples_%480==0){
    const float db=20.f*std::log10(std::max(level_,1e-6f));
    s_.channel_level_dbfs=db;
    if(!noise_ready_){noise_floor_db_=db;noise_ready_=true;}
    else if(db<noise_floor_db_)noise_floor_db_=0.65f*noise_floor_db_+0.35f*db;
    else noise_floor_db_+=0.002f*(db-noise_floor_db_);
    s_.relative_db=std::clamp(db-noise_floor_db_,0.f,80.f);
    const float coherence=level_/std::sqrt(std::max(1e-12f,level_pow_));
    // This statistical full-carrier heuristic helps when tuning onto an
    // already-transmitting station; not a calibrated power/SNR measurement.
    s_.carrier_detected=(db>-55.f && coherence>0.927f);
    if(s_.squelch_db==0)s_.squelch_open=true; // clearly documented diagnostic bypass
    else if(squelch_samples_<9600)s_.squelch_open=false; // 200ms warmup
    else if(s_.relative_db>=float(s_.squelch_db)+1.5f || s_.carrier_detected){
      s_.squelch_open=true;squelch_hang_=squelch_samples_+24000; // 500ms hang
    } else if(s_.relative_db<float(s_.squelch_db)-1.5f && squelch_samples_>squelch_hang_){
      s_.squelch_open=false;
    }
  }
  const float scaled=lp_b_*3.0f*float(s_.volume)/100.0f;
  // Saturation keeps >6 dB output headroom; no full-scale overshoot.
  const float bounded=std::clamp(scaled,-0.70f,0.70f);
  const bool silence=s_.user_muted || s_.volume==0 || !s_.squelch_open || !s_.audio_ready;
  pcm_emit(silence?0:static_cast<int16_t>(std::lround(bounded*32767)),cb,ctx);
}
void Controller::fft_update(const uint8_t* cu8,size_t bytes){
  if(bytes<2048)return;
  std::array<std::complex<float>,1024> f{};
  for(unsigned j=0;j<1024;++j){
    unsigned r=0,t=j;for(int b=0;b<10;++b){r=(r<<1)|(t&1u);t>>=1;}
    const float w=float(0.5-0.5*std::cos(2*pi*j/1023));
    f[r]={float(int(cu8[2*j])-127.5f)*inv_u8*w,float(int(cu8[2*j+1])-127.5f)*inv_u8*w};
  }
  for(unsigned len=2;len<=1024;len*=2){
    const float angle=float(-2*pi/len);const std::complex<float> step(std::cos(angle),std::sin(angle));
    for(unsigned begin=0;begin<1024;begin+=len){
      std::complex<float> tw(1,0);
      for(unsigned x=0;x<len/2;++x){auto p=f[begin+x],q=f[begin+x+len/2]*tw;
        f[begin+x]=p+q;f[begin+x+len/2]=p-q;tw*=step;}
    }
  }
  for(int x=0;x<256;++x){
    const int64_t wanted=int64_t(s_.selected_hz)+(x-128)*2500LL;
    const int64_t relative=wanted-int64_t(s_.applied_center_hz);
    const int index=int(std::llround(double(relative)*1024/double(s_.input_rate)))&1023;
    const float db=20.f*std::log10(std::max(1e-8f,std::abs(f[size_t(index)])/512.0f));
    s_.spectrum[size_t(x)]=uint8_t(std::clamp(std::lround((db+80.f)*255.f/80.f),0l,255l));
  }
  for(size_t r=kWaterfall-1;r>0;--r)s_.waterfall[r]=s_.waterfall[r-1];
  s_.waterfall[0]=s_.spectrum;s_.waterfall_rows=uint8_t(std::min<size_t>(kWaterfall,s_.waterfall_rows+1));
  s_.has_spectrum=true;
}
bool Controller::consume(const uint8_t* data,size_t bytes,const orc::linux_sdr::IqMetadata& meta,PcmCallback cb,void* ctx){
  if(!data||!bytes||(bytes&1u)||bytes>kMaxIqBytes){++s_.rejected_blocks;return false;}
  if(s_.connection!=Connection::ready||s_.tune_pending||meta.receiver_generation!=s_.generation||
     meta.frequency_hz!=s_.applied_center_hz||meta.sample_rate_sps!=s_.input_rate){++s_.rejected_blocks;return false;}
  if(meta.discontinuity||meta.dropped_samples_before||
     (have_continuity_&&next_sample_!=meta.first_sample_index)){
    ++s_.discontinuities;reset_dsp();
  }
  have_continuity_=true;next_sample_=meta.first_sample_index+bytes/2;
  if(fft_delay_++>=31){fft_update(data,bytes);fft_delay_=0;}
  const float inv=inv_cic_quant/(float(decimate_)*float(decimate_)*float(decimate_));
  for(size_t at=0;at<bytes;at+=2){
    const float ri=(float(data[at])-127.5f)*inv_u8,rq=(float(data[at+1])-127.5f)*inv_u8;
    const int32_t xi=int32_t(std::lround((ri*nco_re_-rq*nco_im_)*4096.f));
    const int32_t xq=int32_t(std::lround((ri*nco_im_+rq*nco_re_)*4096.f));
    auto step=[&](Cic& c,int32_t v){c.a=add_signed(c.a,v);c.b+=c.a;c.c+=c.b;};
    step(cic_i_,xi);step(cic_q_,xq);
    const float nr=nco_re_*nco_step_re_-nco_im_*nco_step_im_;
    nco_im_=nco_re_*nco_step_im_+nco_im_*nco_step_re_;nco_re_=nr;
    if((++nco_count_&4095u)==0){const float mag=std::sqrt(nco_re_*nco_re_+nco_im_*nco_im_);
      if(mag>0){nco_re_/=mag;nco_im_/=mag;}}
    if(++decim_phase_==decimate_){
      decim_phase_=0;
      auto comb=[](Cic& c){const uint64_t d1=c.c-c.d1;c.d1=c.c;
        const uint64_t d2=d1-c.d2;c.d2=d1;
        const uint64_t d3=d2-c.d3;c.d3=d2;return signed_mod(d3);};
      const float i=float(comb(cic_i_))*inv,q=float(comb(cic_q_))*inv;
      process_80k(i,q,cb,ctx);
    }
  }
  // Flush available complete audio frames per valid IQ block; no stale partial
  // audio accumulates across retune/disconnect, which reset_dsp() clears.
  if(pcm_size_){if(cb)cb(ctx,pcm_.data(),pcm_size_);pcm_size_=0;}
  return true;
}
}

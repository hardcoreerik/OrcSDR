// SPDX-License-Identifier: AGPL-3.0-only
#include "acars_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace orcsdr::acars {
uint16_t crc16_kermit(const uint8_t* data, size_t n) {
  uint16_t c=0;
  for (size_t j=0;j<n;++j) {
    c^=data[j];
    for (int b=0;b<8;++b) c=(c&1)?(c>>1)^0x8408:(c>>1);
  }
  return c;
}
static uint16_t crc_update_zero(uint16_t c) {
  for (int b=0;b<8;++b) c=(c&1)?(c>>1)^0x8408:(c>>1);
  return c;
}
uint8_t odd_parity_byte(uint8_t raw) {
  raw &= 0x7f;
  uint8_t v=raw;
  v^=v>>4;v^=v>>2;v^=v>>1;
  return uint8_t(raw|((v&1)?0:0x80));
}
static bool odd(uint8_t value) {
  value^=value>>4; value^=value>>2; value^=value>>1;
  return (value&1)!=0;
}
static bool integrity(const uint8_t* buf,size_t n) {
  if (n<15 || crc16_kermit(buf,n)!=0) return false;
  for(size_t j=0;j+2<n;++j) if (!odd(buf[j]))return false;
  return true;
}
bool validate_or_repair(uint8_t* data, size_t n, bool& repaired, uint8_t& fixed) {
  repaired=false;fixed=0;
  if(n<15 || n>kMaxBlockBytes) return false;
  const uint16_t residue=crc16_kermit(data,n);
  if(residue==0) return integrity(data,n);
  // Two observed bad-parity bytes narrow a two-error search to only 8x8.
  // There is never an unbounded recursive correction search in the callback.
  size_t suspects[2]{};int parity_bad=0;
  for(size_t j=0;j<n-2;++j)if(!odd(data[j])){
    if(parity_bad<2)suspects[parity_bad]=j;
    ++parity_bad;
  }
  if(parity_bad>2)return false;

  // CRC linearity: syndrome for a bit at distance d bytes from end is the
  // one-hot CRC after d appended zero bytes. Only eight 16-bit temporaries.
  uint16_t s[8];
  for(int bit=0;bit<8;++bit) {
    const uint8_t b=uint8_t(1u<<bit);
    s[bit]=crc16_kermit(&b,1);
  }
  uint16_t s0[8]{},s1[8]{};
  bool found=false;size_t candidate_byte=0;int candidate_bit=0;
  for(size_t dist=0; dist<n; ++dist) {
    const size_t at=n-1-dist;
    if(parity_bad<=1)for(int bit=0;bit<8;++bit)if(s[bit]==residue) {
      if(at<n-2 && odd(data[at]))continue;
      if(found)return false; // collision: do not choose arbitrarily
      found=true;candidate_byte=at;candidate_bit=bit;
    }
    if(parity_bad==2){
      if(at==suspects[0])for(int b=0;b<8;++b)s0[b]=s[b];
      if(at==suspects[1])for(int b=0;b<8;++b)s1[b]=s[b];
    }
    for(int bit=0;bit<8;++bit)s[bit]=crc_update_zero(s[bit]);
  }
  if(found) {
    data[candidate_byte]^=uint8_t(1u<<candidate_bit);
    if(integrity(data,n)) {repaired=true;fixed=1;return true;}
    data[candidate_byte]^=uint8_t(1u<<candidate_bit);
    return false;
  }
  if(parity_bad!=2)return false;
  bool pair_found=false;int b0=0,b1=0;
  for(int i=0;i<8;++i)for(int j=0;j<8;++j)
    if(uint16_t(s0[i]^s1[j])==residue){
      if(pair_found)return false;
      pair_found=true;b0=i;b1=j;
    }
  if(!pair_found)return false;
  data[suspects[0]]^=uint8_t(1u<<b0);
  data[suspects[1]]^=uint8_t(1u<<b1);
  if(integrity(data,n)){repaired=true;fixed=2;return true;}
  data[suspects[0]]^=uint8_t(1u<<b0);
  data[suspects[1]]^=uint8_t(1u<<b1);
  return false;
}

void FrameDecoder::reset() {
  state_=State::hunt;sync_=0;prekey_run_=0;prekey_window_=0;
  polarity_=0;partial_=0;bit_index_=0;size_=0;
}
void FrameDecoder::drop(bool overflow) {
  if(overflow)++stats_.overflows;else ++stats_.framing_rejected;
  reset();
}
void FrameDecoder::push_bit(bool bit) {
  ++stats_.bits;
  // Experiment: a sustained 2400 Hz pre-key (all ones) opens a short
  // re-acquisition window. This detector costs O(1) integer operations per
  // bit (2400 times/sec), not per IQ sample.
  if(bit) {
    if(prekey_run_<65535) ++prekey_run_;
  } else {
    if(prekey_run_>=16) prekey_window_=48;
    prekey_run_=0;
  }
  if(prekey_window_>0) --prekey_window_;
  sync_=(sync_>>1)|(uint32_t(bit)<<23);
  constexpr uint32_t kSync=0x011616;
  uint32_t normal=sync_^kSync;
  uint32_t inverted=(sync_^0xffffffu)^kSync;
  auto within_one=[](uint32_t d) { return d==0 || (d&(d-1))==0; };
  const bool normal_sync=within_one(normal);
  const bool inverted_sync=within_one(inverted);
  if((state_==State::hunt || prekey_window_>0) &&
      (normal_sync || inverted_sync)) {
    // The prekey gate also permits an interrupted (formerly locked) frame
    // to give up immediately to a new burst's sync. A CRC continues to
    // guard acceptance; retain the stats counters across reset.
    polarity_=normal_sync?0:1;
    state_=State::collect;
    partial_=0;bit_index_=0;size_=0;
    prekey_window_=0;prekey_run_=0;
    ++stats_.sync_candidates;
    return;
  }
  if(state_==State::hunt) return;
  if(bit ^ (polarity_!=0)) partial_|=uint8_t(1u<<bit_index_);
  if(++bit_index_==8){consume_byte(partial_);partial_=0;bit_index_=0;}
}
void FrameDecoder::consume_byte(uint8_t b) {
  // Experiment: defer committing to the ETX/ETB delimiter until the
  // following BCS has verified. One-bit errors in the delimiter can then
  // be located and corrected using the original parity+CRC evidence.
  // A BCS octet that happens to equal DEL is not treated as definitive.
  // This operates at 300 bytes/s, not in the sample-rate DSP loop.
  if(size_>=raw_.size()) {drop(true);return;}
  auto plausible_del=[](uint8_t v) {
    const uint8_t d=uint8_t(v^0x7f);
    return d==0 || (d&(d-1))==0;
  };
  if(size_>=15 && plausible_del(b)) {
    if(emit()) {reset();return;}
  }
  raw_[size_++]=b;
}
static bool body_format_ok(const uint8_t* raw,size_t n) {
  if(n<15 || n>kMaxBlockBytes) return false;
  for(size_t i=0;i<n-2;++i)if(!odd(raw[i]))return false;
  if((raw[n-3]&0x7f)!=0x03 && (raw[n-3]&0x7f)!=0x17) return false;
  if(n>=16 && (raw[12]&0x7f)!=0x02 && (raw[12]&0x7f)!=0x03) return false;
  return true;
}
bool FrameDecoder::emit() {
  if(size_<15 || size_>kMaxBlockBytes) {++stats_.framing_rejected;return false;}
  // Work on an isolated candidate: a CRC-corrected byte sequence can still
  // violate ACARS grammar. In that case the live framing buffer MUST remain
  // unchanged for the next candidate delimiter (a BCS byte may resemble DEL).
  std::array<uint8_t,kMaxBlockBytes> candidate{};
  std::copy_n(raw_.data(),size_,candidate.data());
  bool corrected=false;uint8_t corrected_bits=0;
  if(!validate_or_repair(candidate.data(),size_,corrected,corrected_bits)){
    ++stats_.crc_rejected;return false;
  }
  if(!body_format_ok(candidate.data(),size_)){++stats_.framing_rejected;return false;}
  // An ordinary VHF block carries no more than 220 text characters.
  if(size_>16 && (candidate[12]&0x7f)==0x02 && size_-16>220) {
    ++stats_.framing_rejected;return false;
  }
  Message m{};m.frequency_hz=frequency_hz_;m.raw_bytes=size_;
  m.corrected=corrected;m.corrected_bits=corrected_bits;
  m.end_marker=uint8_t(candidate[size_-3]&0x7f);
  m.mode=char(candidate[0]&0x7f);
  for(int i=0;i<7;++i)m.tail[i]=char(candidate[1+i]&0x7f);
  m.acknowledgement=char(candidate[8]&0x7f);
  m.label[0]=char(candidate[9]&0x7f);m.label[1]=char(candidate[10]&0x7f);
  m.block_id=char(candidate[11]&0x7f);
  if(size_>16 && (candidate[12]&0x7f)==0x02) {
    m.has_text=true;
    // Text includes ACARS message number and flight identifier when present.
    const size_t begin=13,end=size_-3;
    m.text_length=uint16_t(end-begin);
    for(size_t i=0;i<end-begin;++i)m.text[i]=char(candidate[begin+i]&0x7f);
    if(m.text_length>=10 && m.block_id>='0' && m.block_id<='9') {
      for(int i=0;i<6;++i)m.flight[i]=m.text[4+i];
    }
  }
  ++stats_.crc_ok;
  if(corrected)++stats_.corrected;
  if(callback_)callback_(context_,m);
  return true;
}

void Decoder::reset_stream() {
  for(auto& f:frames_)f.reset();                // FrameDecoder::reset keeps its counters
  accum_.fill(0.f);state_.fill(true);
  dc_=i1_=q1_=i2_=q2_=pi_=pq_=0.f;
  mixer_i_=1.f;mixer_q_=0.f;osc_phase_=bit_phase_=0;signal_initialized_=false;
  last_hash_=0;last_hash_sample_=0;have_last_=false;
}
void Decoder::reset() {
  reset_stream();
  unique_ok_=unique_corrected_=0;sample_index_=0;
  for(auto& f:frames_)f.set_callback(&Decoder::on_phase_message,this);
}
void Decoder::set_frequency(uint32_t hz){for(auto& f:frames_)f.set_frequency(hz);}
void Decoder::on_phase_message(void* self,const Message& message){static_cast<Decoder*>(self)->accept(message);}
void Decoder::accept(const Message& m){
  // FNV-1a over everything that identifies a block. The same block recovered by a neighbouring bit phase arrives within
  // one bit time (10 samples); two genuine blocks are always at least 15 bytes (1200 samples) apart.
  uint32_t h=2166136261u;
  auto mix=[&h](uint8_t b){h^=b;h*=16777619u;};
  mix(uint8_t(m.mode));mix(uint8_t(m.acknowledgement));mix(uint8_t(m.block_id));mix(uint8_t(m.label[0]));mix(uint8_t(m.label[1]));
  for(int i=0;i<7;++i)mix(uint8_t(m.tail[i]));
  mix(m.end_marker);
  for(unsigned i=0;i<m.text_length&&i<kMaxBlockBytes;++i)mix(uint8_t(m.text[i]));
  if(have_last_&&h==last_hash_&&sample_index_-last_hash_sample_<kSamplesPerBit*2)return;
  have_last_=true;last_hash_=h;last_hash_sample_=sample_index_;
  ++unique_ok_;if(m.corrected)++unique_corrected_;
  if(callback_)callback_(context_,m);
}
const Stats& Decoder::stats() const {
  Stats best{};
  for(const auto& f:frames_){
    const Stats& s=f.stats();
    if(s.sync_candidates>best.sync_candidates)best.sync_candidates=s.sync_candidates;
    if(s.crc_rejected>best.crc_rejected)best.crc_rejected=s.crc_rejected;
    if(s.framing_rejected>best.framing_rejected)best.framing_rejected=s.framing_rejected;
    if(s.overflows>best.overflows)best.overflows=s.overflows;
  }
  best.bits=frames_[0].stats().bits;
  best.crc_ok=unique_ok_;best.corrected=unique_corrected_;
  stats_=best;
  return stats_;
}
void Decoder::process_channel_iq(float i,float q) {
  // Envelope AM demod. No RF phase tracking required.
  float mag=std::sqrt(i*i+q*q);
  dc_+=0.00495f*(mag-dc_); // ~19 Hz DC pole at 24 kS/s
  float sample=mag-dc_;
  // Heterodyne 1800 Hz midpoint to DC, fixed oscillator (24k / 1800).
  float mi=sample*mixer_i_,mq=-sample*mixer_q_;
  const float si=0.891006524188368f,sq=0.453990499739547f; // 2*pi*1800/24000
  float oi=mixer_i_*si-mixer_q_*sq;
  float oq=mixer_i_*sq+mixer_q_*si;
  mixer_i_=oi;mixer_q_=oq;
  if(++osc_phase_==40){osc_phase_=0;float norm=1.f/std::sqrt(mixer_i_*mixer_i_+mixer_q_*mixer_q_);mixer_i_*=norm;mixer_q_*=norm;}
  // Two poles suppress the -3000/-4200 Hz images; discriminated phase
  // determines which of the two MSK tones is present.
  constexpr float alpha=0.38f;
  i1_+=alpha*(mi-i1_);q1_+=alpha*(mq-q1_);
  i2_+=alpha*(i1_-i2_);q2_+=alpha*(q1_-q2_);
  float cross=pi_*q2_-pq_*i2_;
  float dot=pi_*i2_+pq_*q2_;
  pi_=i2_;pq_=q2_;
  ++sample_index_;
  if(!signal_initialized_) {signal_initialized_=true;return;}
  // Cross phase normalized by instantaneous power; atan2 would cost a
  // transcendental per sample, while atan2 sign is enough for two tones.
  const float discriminator=cross/(std::abs(dot)+std::abs(cross)+1.e-12f);
  for(float& a:accum_)a+=discriminator;
  // Ten overlapping integrate-and-dump windows, one ending on each sample phase.
  const int p=bit_phase_;
  if(++bit_phase_>=kSamplesPerBit)bit_phase_=0;
  const bool change=accum_[p]<0.f;
  accum_[p]=0.f;
  state_[p]=state_[p]^change;
  frames_[p].push_bit(state_[p]);
}

static void fir_design(float* h,int n,double cutoff,double sample_rate){
  // Blackman-windowed sinc; cutoff here is 0..Nyquist in Hz.
  constexpr double pi=3.14159265358979323846;
  double s=0;
  for(int k=0;k<n;++k){
    double x=k-(n-1)/2.0;
    double y=x==0?2*cutoff/sample_rate:std::sin(2*pi*cutoff*x/sample_rate)/(pi*x);
    double w=0.42-0.5*std::cos(2*pi*k/(n-1))+0.08*std::cos(4*pi*k/(n-1));
    h[k]=float(y*w);s+=h[k];
  }
  for(int k=0;k<n;++k)h[k]=float(h[k]/s);
}
void Frontend2400k::design_taps(){
  fir_design(taps1_.data(),kStage1Len,42000.,2400000.);
  fir_design(taps2_.data(),kStage2Len,8800.,120000.);
  taps_ready_=true;
}
void Frontend2400k::reset(){
  if(!taps_ready_)design_taps();
  ring1_={};ring2_={};pos1_=pos2_=count1_=count2_=0;
  osc_i_=1.f;osc_q_=0.f;renorm_=0;
}
void Frontend2400k::set_channel_offset_hz(int32_t hz){
  const double angle=-2.0*3.14159265358979323846*hz/2400000.0;
  step_i_=float(std::cos(angle));step_q_=float(std::sin(angle));
}
void Frontend2400k::process_cu8(const uint8_t* iq,size_t n,
  void(*emit)(void*,float,float),void* ctx){
  if(!taps_ready_)reset();
  for(size_t x=0;x<n;++x){
    const float ii=(float(iq[2*x])-127.5f)/128.f;
    const float qq=(float(iq[2*x+1])-127.5f)/128.f;
    float ri=ii*osc_i_-qq*osc_q_;
    float rq=ii*osc_q_+qq*osc_i_;
    float ni=osc_i_*step_i_-osc_q_*step_q_;
    osc_q_=osc_i_*step_q_+osc_q_*step_i_;osc_i_=ni;
    if(++renorm_==2048){renorm_=0;float norm=1.f/std::sqrt(osc_i_*osc_i_+osc_q_*osc_q_);osc_i_*=norm;osc_q_*=norm;}
    ring1_[pos1_]={ri,rq};
    if(++pos1_==kStage1Len)pos1_=0;
    if(++count1_<20)continue;
    count1_=0;
    // Windowed-sinc taps are symmetric. Pair opposite samples before each
    // multiply: (101+1)/2 taps instead of 101, without extra memory.
    float i1=0,q1=0;
    int newest=pos1_?pos1_-1:kStage1Len-1;
    int oldest=pos1_;
    for(int j=0;j<kStage1Len/2;++j){
      const float tap=taps1_[j];
      i1+=tap*(ring1_[newest].i+ring1_[oldest].i);
      q1+=tap*(ring1_[newest].q+ring1_[oldest].q);
      if(--newest<0)newest=kStage1Len-1;
      if(++oldest==kStage1Len)oldest=0;
    }
    i1+=taps1_[kStage1Len/2]*ring1_[newest].i;
    q1+=taps1_[kStage1Len/2]*ring1_[newest].q;
    ring2_[pos2_]={i1,q1};
    if(++pos2_==kStage2Len)pos2_=0;
    if(++count2_<5)continue;
    count2_=0;
    float i2=0,q2=0;
    newest=pos2_?pos2_-1:kStage2Len-1;
    oldest=pos2_;
    for(int j=0;j<kStage2Len/2;++j){
      const float tap=taps2_[j];
      i2+=tap*(ring2_[newest].i+ring2_[oldest].i);
      q2+=tap*(ring2_[newest].q+ring2_[oldest].q);
      if(--newest<0)newest=kStage2Len-1;
      if(++oldest==kStage2Len)oldest=0;
    }
    i2+=taps2_[kStage2Len/2]*ring2_[newest].i;
    q2+=taps2_[kStage2Len/2]*ring2_[newest].q;
    emit(ctx,i2,q2);
  }
}

void Frontend240k::design_taps(){
  // 9 kHz cutoff at 240 kS/s: passes the AM carrier with +-3.6 kHz of MSK sidebands plus a few kHz of oscillator error,
  // and is down by about 50 dB at 15 kHz, where the 24 kS/s output starts to alias.
  float full[kTaps];
  fir_design(full,kTaps,9000.,240000.);
  for(int k=0;k<=kTaps/2;++k)taps_[k]=full[k];
  for(int v=0;v<256;++v)lut_[v]=(float(v)-127.5f)/127.5f;
  tables_ready_=true;
}
void Frontend240k::reset(){
  if(!tables_ready_)design_taps();
  work_i_.fill(0.f);work_q_.fill(0.f);phase_=0;rotation_=0;
}
void Frontend240k::process_cu8(const uint8_t* iq,size_t n,void(*emit)(void*,float,float),void* ctx){
  constexpr int H=kTaps-1;
  float* wi=work_i_.data()+H;
  float* wq=work_q_.data()+H;
  while(n>0){
    const int chunk=int(n<size_t(kChunk)?n:size_t(kChunk));
    // Mix to DC: multiply by exp(-j*pi/2*n), i.e. (i,q) -> (i,q), (q,-i), (-i,-q), (-q,i).
    int r=rotation_;
    for(int k=0;k<chunk;++k){
      const float a=lut_[iq[2*k]],b=lut_[iq[2*k+1]];
      switch(r){
        case 0: wi[k]=a;  wq[k]=b;  break;
        case 1: wi[k]=b;  wq[k]=-a; break;
        case 2: wi[k]=-a; wq[k]=-b; break;
        default: wi[k]=-b; wq[k]=a; break;
      }
      r=(r+1)&3;
    }
    rotation_=r;
    // Outputs land on the sample that completes each group of kDecimation inputs; the window is the kTaps ending there.
    for(int k=kDecimation-1-phase_;k<chunk;k+=kDecimation){
      const float* ci=work_i_.data()+k;      // window = work[k .. k+kTaps-1]
      const float* cq=work_q_.data()+k;
      float oi=0.f,oq=0.f;
      for(int t=0;t<kTaps/2;++t){            // symmetric taps: fold the window around its centre
        oi+=taps_[t]*(ci[t]+ci[kTaps-1-t]);
        oq+=taps_[t]*(cq[t]+cq[kTaps-1-t]);
      }
      oi+=taps_[kTaps/2]*ci[kTaps/2];
      oq+=taps_[kTaps/2]*cq[kTaps/2];
      emit(ctx,oi,oq);
    }
    phase_=(phase_+chunk)%kDecimation;
    // Keep the newest kTaps-1 samples as the next chunk's history.
    for(int t=0;t<H;++t){work_i_[t]=work_i_[chunk+t];work_q_[t]=work_q_[chunk+t];}
    iq+=2*size_t(chunk);
    n-=size_t(chunk);
  }
}
} // namespace orcsdr::acars

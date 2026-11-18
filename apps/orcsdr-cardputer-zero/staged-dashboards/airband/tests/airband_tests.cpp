#include "airband.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

using namespace orc::zero::airband;
namespace {
constexpr double pi=3.14159265358979323846;
void check(bool ok,const char* test){if(!ok){std::fprintf(stderr,"FAIL: %s\n",test);std::exit(1);}}
struct Capture {std::vector<int16_t> v;};
void capture(void* c,const int16_t* p,size_t n){auto& v=static_cast<Capture*>(c)->v;v.insert(v.end(),p,p+n);}
struct Osc {double re=1,im=0,step_re=1,step_im=0;
  Osc(double hz,uint32_t rate){const double theta=2*pi*hz/rate;step_re=std::cos(theta);step_im=std::sin(theta);}
  void next(){const double nr=re*step_re-im*step_im;im=re*step_im+im*step_re;re=nr;}
};
struct Source {
  uint32_t rate=0;uint64_t sample=0;
  Osc a,m,b,other;
  bool wanted=true,interferer=false,dc=false;
  explicit Source(uint32_t r,int32_t center_offset):rate(r),a(-double(center_offset),r),m(1000,r),
     b(25000.0-center_offset,r),other(1800,r){}
  void fill(std::vector<uint8_t>& out){
    for(size_t j=0;j<out.size()/2;++j){
      double x=0,y=0;
      if(wanted){const double env=0.38*(1+0.55*m.im);x+=env*a.re;y+=env*a.im;}
      if(interferer){const double env=0.50*(1+0.75*other.im);x+=env*b.re;y+=env*b.im;}
      if(dc){x+=0.09;y+=0.08;}
      out[j*2]=uint8_t(std::clamp(std::lround(127.5+128*x),0l,255l));
      out[j*2+1]=uint8_t(std::clamp(std::lround(127.5+128*y),0l,255l));
      a.next();m.next();b.next();other.next();++sample;
    }
  }
};
struct ReceiverTest {
  Controller c;Capture audio;uint32_t rate;uint32_t center;
  explicit ReceiverTest(uint32_t r):rate(r){
    check(c.activate(121500000),"activate");auto requested=c.requested_radio();
    center=requested.rf_center_hz;
    c.radio_applied(center,r,5);c.audio_available(true);
    for(int i=0;i<6;++i)check(c.action(Action::squelch_down),"SQL diag toggle");
    check(!c.snapshot().squelch_db,"SQL diagnostic value");
  }
  void pump(Source& s,size_t samples,size_t block_samples=8192){
    std::vector<uint8_t> iq(2*block_samples);
    for(size_t done=0;done<samples;){
      const size_t n=std::min(block_samples,samples-done);iq.resize(2*n);
      const uint64_t start=s.sample;s.fill(iq);
      orc::linux_sdr::IqMetadata meta{};meta.frequency_hz=center;
      meta.sample_rate_sps=rate;meta.receiver_generation=5;meta.first_sample_index=start;
      check(c.consume(iq.data(),iq.size(),meta,capture,&audio),"valid IQ consumed");
      done+=n;
    }
  }
};
double tone(const std::vector<int16_t>& v,int frequency){
  const size_t start=v.size()>9600?v.size()-9600:0;
  double re=0,im=0;
  for(size_t i=start;i<v.size();++i){const double angle=2*pi*frequency*double(i-start)/48000.0;
    re+=v[i]*std::cos(angle);im+=v[i]*std::sin(angle);}
  return 2*std::hypot(re,im)/double(v.size()-start);
}
void rate_test(uint32_t rate){
  ReceiverTest r(rate);Source s(rate,int32_t(r.center-121500000));
  r.pump(s,rate/3);
  const auto& v=r.audio.v;
  check(v.size()>15000&&v.size()<16500,"rate conversion to 48k");
  const double level=tone(v,1000),off=tone(v,1800);
  check(level>180&&level<20000,"AM real selected-channel 1 kHz recovery");
  check(off<level*0.12,"AM clean tone / low false tone");
  for(int16_t x:v)check(std::abs(int(x))<=22938,"PCM bounded 70 percent");
  std::printf("Rate %u input: %zu output frames, tone amplitude %.1f, unwanted-bin %.1f\n",rate,v.size(),level,off);
  // Exact silence and monotonic volume are not delegated to ALSA.
  ReceiverTest m(rate);m.c.action(Action::mute);
  Source ms(rate,int32_t(m.center-121500000));m.pump(ms,rate/6);
  check(std::all_of(m.audio.v.begin(),m.audio.v.end(),[](int16_t x){return !x;}),"mute exact silence");
  m.c.action(Action::mute);for(int i=0;i<7;++i)m.c.action(Action::volume_down);
  m.audio.v.clear();m.pump(ms,rate/10);
  check(std::all_of(m.audio.v.begin(),m.audio.v.end(),[](int16_t x){return !x;}),"volume zero exact silence");
}
void interference_test(){
  ReceiverTest r(2560000);Source s(r.rate,int32_t(r.center-121500000));s.interferer=true;
  r.pump(s,r.rate/3);
  double wanted=tone(r.audio.v,1000),unwanted=tone(r.audio.v,1800);
  std::printf("Adjacent +25 kHz AM isolation: 1000 Hz %.1f, 1800 Hz %.1f\n",wanted,unwanted);
  check(wanted>150&&unwanted<wanted*0.12,"channel filter adjacent AM rejection + correct offset sign");
  ReceiverTest wrong(2560000);Source only(wrong.rate,int32_t(wrong.center-121500000));
  only.wanted=false;only.interferer=true;wrong.pump(only,wrong.rate/3);
  check(tone(wrong.audio.v,1800)<wanted*0.12,"adjacent-only AM does not demodulate like desired");
}
void chunks_test(){
  ReceiverTest a(2400000),b(2400000);
  Source sa(a.rate,int32_t(a.center-121500000));Source sb(b.rate,int32_t(b.center-121500000));
  a.pump(sa,a.rate/6,2048);b.pump(sb,b.rate/6,8192);
  check(a.audio.v==b.audio.v,"chunk-equivalent PCM incl FIR phase and CIC continuity");
}
void control_test(){
  uint32_t value=0;
  check(parse_frequency_mhz("118.000",value)&&value==kMinHz,"lower band edge");
  check(parse_frequency_mhz("136.975000",value)&&value==kMaxHz,"upper band edge");
  check(parse_frequency_mhz("121.50025",value)&&value==121500250,"decimal 5 digits precise");
  for(const std::string s:{"117.975","137","121.5000001","121x500","121..500","","121.","122.500.1"}){
    if(s=="121.")continue; // valid 121 MHz, no fractional digit required
    check(!parse_frequency_mhz(s,value),"reject malformed or out-of-band frequency");
  }
  check(step_frequency(kMinHz,-1)==kMinHz,"low step bounded");
  check(step_frequency(kMaxHz,1)==kMaxHz,"high step bounded");
  check(step_frequency(kGuardHz,1)==121525000,"true 25 kHz step");
  Controller c;check(c.activate(),"control active");
  auto st=c.settings();st.volume=999;
  check(!c.restore_settings(st),"bad persisted settings rejected");
  st.volume=55;st.squelch_db=10;
  check(c.restore_settings(st)&&c.settings().volume==55&&c.settings().squelch_db==10,
        "validated persisted settings roundtrip");
  auto q=c.requested_radio();check(q.rf_center_hz==121625000,"documented center offset");
  c.radio_applied(q.rf_center_hz,2560000,1);
  check(c.tune_direct(122000000)&&c.snapshot().tune_pending,"tune pending visible");
  c.radio_failed("TUNER NACK");
  check(c.snapshot().selected_hz==kGuardHz&&!c.snapshot().tune_pending,"tune failure restores channel identity");
  check(c.snapshot().connection==Connection::error,"failed tune not marked ready");
  c.stop();check(c.snapshot().connection==Connection::off,"stop state");
  check(!c.tune_direct(121525000),"inactive tune denied");
  Controller d;d.activate();d.radio_applied(121625000,512000,1);
  check(d.snapshot().connection==Connection::error,"unsupported input rate rejected");
  Controller keyboard;keyboard.activate();
  check(keyboard_action(keyboard,'f')&&keyboard_action(keyboard,'1')&&
        keyboard_action(keyboard,'2')&&keyboard_action(keyboard,'1')&&
        keyboard_action(keyboard,'.')&&keyboard_action(keyboard,'5')&&
        keyboard_action(keyboard,'\r'),"keyboard frequency entry");
  check(keyboard.snapshot().selected_hz==121500000,"keyboard input correct");
  check(!keyboard_action(keyboard,' ')&&!keyboard_action(keyboard,'p')&&
        !keyboard_action(keyboard,'q'),"global selector, palette and exit unclaimed");
}
void discontinuity_test(){
  ReceiverTest r(2400000);Source s(r.rate,int32_t(r.center-121500000));r.pump(s,20000);
  auto snap=r.c.snapshot();
  std::vector<uint8_t> iq(16384);s.fill(iq);
  orc::linux_sdr::IqMetadata old{};old.frequency_hz=r.center;old.sample_rate_sps=r.rate;
  old.receiver_generation=4;old.first_sample_index=s.sample-iq.size()/2;
  check(!r.c.consume(iq.data(),iq.size(),old,capture,&r.audio),"stale generation rejected");
  old.receiver_generation=5;old.discontinuity=true;
  check(r.c.consume(iq.data(),iq.size(),old,capture,&r.audio),"explicit discontinuity resets safely");
  check(r.c.snapshot().discontinuities==snap.discontinuities+1,"known discontinuity counted");
  old.first_sample_index+=iq.size()/2+27;old.discontinuity=false;
  check(r.c.consume(iq.data(),iq.size(),old,capture,&r.audio),"index discontinuity resets");
  check(r.c.snapshot().discontinuities==snap.discontinuities+2,"index discontinuity counted");
  check(!r.c.consume(iq.data(),3,old,capture,&r.audio),"odd IQ rejected");
  check(!r.c.consume(iq.data(),kMaxIqBytes+2,old,capture,&r.audio),"oversized IQ rejected");
  r.c.disconnected("UNPLUGGED");check(r.c.snapshot().connection==Connection::error,"receiver lost");
  check(!r.c.consume(iq.data(),iq.size(),old,capture,&r.audio),"no stale playback after disconnect");
}
void sql_test(){
  ReceiverTest r(2400000);
  for(int k=0;k<8;++k)r.c.action(Action::squelch_up); // 8 dB + hysteresis
  // Start with no carrier to learn relative noise floor.
  Source s(r.rate,int32_t(r.center-121500000));s.wanted=false;
  r.pump(s,r.rate/4);
  check(!r.c.snapshot().squelch_open,"no carrier remains closed");
  s.wanted=true;r.pump(s,r.rate/2);
  check(r.c.snapshot().squelch_open,"carrier opens squelch above learned noise floor");
  r.c.action(Action::mute);
  auto pre=r.audio.v.size();r.pump(s,r.rate/10);
  check(std::all_of(r.audio.v.begin()+pre,r.audio.v.end(),[](int16_t v){return v==0;}),"user mute independent of squelch");
  s.wanted=false;r.pump(s,r.rate*13/10);
  check(!r.c.snapshot().squelch_open,"squelch closes after 500ms hang");
  check(r.c.snapshot().user_muted,"squelch close does not lift user mute");
  ReceiverTest already_active(2400000);
  for(int n=0;n<6;++n)already_active.c.action(Action::squelch_up);
  Source carrier(already_active.rate,int32_t(already_active.center-121500000));
  already_active.pump(carrier,already_active.rate/3);
  check(already_active.c.snapshot().squelch_open,"carrier-present-at-entry has independent gate");
}
void safety_test(){
  ReceiverTest quiet(2400000);Source src(quiet.rate,int32_t(quiet.center-121500000));
  src.wanted=false;src.dc=true;
  quiet.pump(src,quiet.rate/3);
  const auto& v=quiet.audio.v;
  const auto at=v.size()>4800?v.size()-4800:0;
  int maxv=0;for(size_t i=at;i<v.size();++i)maxv=std::max(maxv,std::abs(int(v[i])));
  check(maxv<250,"unmodulated carrier/DC offset no sustained loud audio");
  ReceiverTest strong(2560000);Source high(strong.rate,int32_t(strong.center-121500000));
  high.dc=true;high.interferer=true;strong.pump(high,strong.rate/4);
  for(int16_t x:strong.audio.v)check(std::abs(int(x))<=22938,"strong input bounded output");
  strong.c.audio_available(false,"PCM SINK OPEN FAILED");
  const size_t before=strong.audio.v.size();strong.pump(high,strong.rate/10);
  check(std::all_of(strong.audio.v.begin()+before,strong.audio.v.end(),[](int16_t x){return x==0;}),
        "audio sink failure gates exact silence without reconnect loop");
  check(!strong.c.snapshot().audio_ready&&strong.c.snapshot().error[0],"audio error observable");
  const size_t count=strong.audio.v.size();
  check(strong.c.tune_direct(121525000),"retune request");
  std::vector<uint8_t> stale(8192);const auto start=high.sample;high.fill(stale);
  orc::linux_sdr::IqMetadata meta{};meta.frequency_hz=strong.center;
  meta.sample_rate_sps=strong.rate;meta.receiver_generation=5;meta.first_sample_index=start;
  check(!strong.c.consume(stale.data(),stale.size(),meta,capture,&strong.audio),"pending tune rejects old IQ");
  check(strong.audio.v.size()==count,"no previous-generation audio queued on pending retune");
  const auto rf=strong.c.requested_radio();strong.c.radio_applied(rf.rf_center_hz,strong.rate,6);
  check(strong.c.snapshot().generation==6,"new generation accepted");
  check(!strong.c.consume(stale.data(),stale.size(),meta,capture,&strong.audio),"old generation after readback rejected");
}
void visual_test(){
  Renderer rend;Controller c; c.activate();c.set_demo(true);
  rend.render(c.snapshot());const unsigned clears=rend.full_clears();
  auto one=rend.framebuffer();c.action(Action::volume_up);
  rend.render(c.snapshot());
  check(rend.full_clears()==clears,"no full frame clear on value update");
  check(rend.framebuffer()!=one,"volume value produces real changed pixels");
  check(rend.dirty_count()==1 && rend.dirty_rects()[0].y==49,
        "only the changed status region redraws");
  check(sizeof(Renderer)<125000,"bounded RGB565 framebuffer memory");
}
}
int main(){
  rate_test(2400000);rate_test(2560000);interference_test();chunks_test();
  control_test();discontinuity_test();sql_test();safety_test();visual_test();
  std::puts("PASS airband DSP, isolation, continuity, controls, gating and renderer");
}

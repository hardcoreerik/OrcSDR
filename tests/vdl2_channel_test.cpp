// SPDX-License-Identifier: AGPL-3.0-only
// Synthetic end-to-end VDL2 transmit model is intentionally test-only.
#include "vdl2_channel.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include "vdl2_test_signal.hpp"
using namespace orcsdr::vdl2;
using namespace vdl2_test;
namespace {
struct Seen{int frames=0,other=0,acars=0;};
void on_frame(void* ctx,const FrameView& f){
 auto* s=static_cast<Seen*>(ctx);++s->frames;
 if(!f.is_acars)++s->other;
 else {++s->acars; assert(f.acars_message && f.acars_message->text_length==14); assert(f.acars_message->label[0]=='H');}
 assert(f.raw_size>=11);
}
}
int main(){
  const auto payload=make_bits();
  for(const uint32_t rate:{50000u,105000u})for(const float skew:{0.f,20.f}){
    Receiver rx(rate);Seen seen{};
    rx.set_callback(on_frame,&seen);
    const auto samples=modulated(payload,rate,skew);
    for(const auto& s:samples)rx.process_sample(s.i,s.q);
    std::fprintf(stderr,"rate=%u cfo=%g frames=%d uwCand=%u lock=%u hdr=%u/%u fec=%u/%u flags=%u good=%u bad=%u\n",
      rate,skew,seen.frames,rx.stats().uw_candidates,rx.stats().uw_locked,
      rx.stats().header_ok,rx.stats().header_fail,rx.stats().fec_ok,rx.stats().fec_fail,
      rx.frames_stats().flags,rx.frames_stats().frames_crc_ok,rx.frames_stats().frames_crc_bad);
    assert(seen.frames>=1);
  }
  for(const auto rate:{250000u,1050000u}){
    Channel channel;Seen seen{};
    const unsigned output=rate==250000u?50000u:105000u;
    assert(channel.configure(rate,output));
    channel.receiver().set_callback(on_frame,&seen);
    const auto waveform=modulated(payload,rate,10.f);
    channel.process_complex(waveform.data(),waveform.size());
    std::fprintf(stderr,"channel rate %u -> %u valid=%d lock=%u hdr=%u fec=%u\n",rate,output,seen.frames,channel.receiver().stats().uw_locked,channel.receiver().stats().header_ok,channel.receiver().stats().fec_ok);
    assert(seen.frames>=1);
  }
  // Nonzero tuning offset: producer RF center != desired VDL2 channel.
  for(unsigned input:{250000u,1050000u}){
    Channel channel;Seen seen{};
    const unsigned output=input==250000u?50000u:105000u;
    assert(channel.configure(input,output,25000));
    channel.receiver().set_callback(on_frame,&seen);
    const auto waveform=modulated(payload,input,25010.f);
    // Real streaming USB bursts are chunked, and chunk boundaries must not matter.
    for(size_t pos=0;pos<waveform.size();pos+=257){
      channel.process_complex(waveform.data()+pos,std::min<size_t>(257,waveform.size()-pos));
    }
    std::fprintf(stderr,"DDC offset 25k at %u valid=%d header=%u fec=%u\n",input,seen.frames,channel.receiver().stats().header_ok,channel.receiver().stats().fec_ok);
    assert(seen.frames>=1);
  }
  const auto aoa=make_bits(true);
  Channel on{};Seen delivered{};
  assert(on.configure(1050000,105000));on.receiver().set_callback(on_frame,&delivered);
  const auto traffic=modulated(aoa,1050000,8.f);
  on.process_complex(traffic.data(),traffic.size());
  assert(delivered.frames==1 && delivered.acars==1);
  std::puts("vdl2_channel_test passed (synthetic IQ, downmix, decimation, FEC, AVLC)");
}

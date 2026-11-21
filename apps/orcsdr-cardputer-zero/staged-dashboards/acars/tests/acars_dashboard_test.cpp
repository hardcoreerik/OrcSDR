// SPDX-License-Identifier: AGPL-3.0-only
#include "acars_dashboard.hpp"
#include "acars_renderer.hpp"
#include "acars_selftest.hpp" // canonical deterministic on-channel CU8 RF generator
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
using namespace orc::zero::acars_dashboard;
namespace {
int failures=0;
#define CHECK(c) do{if(!(c)){std::cerr<<__FILE__<<":"<<__LINE__<<" CHECK failed: "<<#c<<"\n";++failures;}}while(false)
constexpr uint64_t ns=1000000000ULL;
orcsdr::acars::Message msg(const char* s="HELLO"){
  orcsdr::acars::Message m{};m.frequency_hz=kDefaultChannelHz;
  std::memcpy(m.tail,"N123456",7);std::memcpy(m.flight,"UAL111",6);
  m.label[0]='Q';m.label[1]='0';m.has_text=true;m.end_marker=0x03;
  m.text_length=uint16_t(std::strlen(s));m.raw_bytes=uint16_t(m.text_length+16);
  std::memcpy(m.text,s,m.text_length);return m;
}
void test_radio(){
  RadioRequest r{};
  CHECK(make_radio_request(131550000,r));
  CHECK(r.center_hz==131490000&&r.channel_hz==131550000&&r.rate_sps==240000);
  CHECK(!make_radio_request(131551000,r));
  CHECK(!make_radio_request(137025000,r));
  CHECK(!make_radio_request(0,r));
  Dashboard d;
  CHECK(!d.start(RadioRequest{131490000,131550000,2560000},1));
  CHECK(!d.start(RadioRequest{131490100,131550000,240000},1));
  CHECK(d.start(r,1));d.service();
  CHECK(d.snapshot().receiver==ReceiverState::receiving);
  std::array<uint8_t,2> iq{128,128};
  orc::linux_sdr::IqMetadata m{};m.frequency_hz=r.center_hz;m.sample_rate_sps=240000;
  m.receiver_generation=1;m.monotonic_ns=1;
  CHECK(d.consume(iq.data(),2,m));
  m.first_sample_index=1;m.sample_rate_sps=2560000;
  CHECK(!d.consume(iq.data(),2,m));d.service();
  CHECK(d.snapshot().receiver==ReceiverState::error);
  CHECK(d.start(r,2));m.sample_rate_sps=240000;m.receiver_generation=1;
  CHECK(!d.consume(iq.data(),2,m)); // older generation is never accepted
  CHECK(d.start(r,3));m.receiver_generation=3;m.first_sample_index=0;
  CHECK(!d.consume(iq.data(),3,m)); // malformed odd length
}
void test_ring_ui(){
  Dashboard d;d.begin_demo();
  CHECK(!d.input(Input::detail));
  for(int n=0;n<19;++n){auto m=msg();m.text[0]=char('A'+n);CHECK(d.append_demo(m,uint64_t(n+1)*ns));}
  auto s=d.snapshot();CHECK(s.held==kHistory);
  CHECK(s.history[0].message.text[0]=='D');
  CHECK(s.history[kHistory-1].message.text[0]=='S');
  CHECK(s.selected_serial==s.history[kHistory-1].serial);
  CHECK(d.input(Input::detail));s=d.snapshot();CHECK(s.detail&&!s.history[s.selected].unread);
  CHECK(d.input(Input::back));CHECK(!d.snapshot().detail);
  for(int n=0;n<15;++n)CHECK(d.input(Input::previous));
  CHECK(!d.input(Input::previous));
  s=d.snapshot();const auto original=s.selected_serial;
  auto m=msg("NEW");CHECK(d.append_demo(m,20*ns));
  s=d.snapshot();CHECK(s.held==kHistory);
  CHECK(s.selected_serial!=original); // evicted selection clamps to oldest surviving record
  CHECK(d.input(Input::detail));
  CHECK(d.append_demo(msg("?"),21*ns)); // additional ring activity while overlay is open
}
void test_render_payload(){
  auto m=msg();m.text_length=orcsdr::acars::kMaxBlockBytes;
  m.has_text=true;m.end_marker=0x17;m.corrected=true;m.corrected_bits=2;
  for(size_t i=0;i<m.text_length;++i)m.text[i]=(i%2)?char('A'+i%26):char(0x1b);
  const std::string escaped=escaped_payload(m);
  CHECK(escaped.find("<1B>")!=std::string::npos);
  CHECK(escaped.size()<=4*orcsdr::acars::kMaxBlockBytes);
  CHECK(detail_line_count(m)>=1);
  Dashboard d;d.begin_demo();CHECK(d.append_demo(m,2*ns));
  Renderer renderer;renderer.render(d.snapshot(),3*ns);
  const auto original=renderer.pixels();
  CHECK(renderer.full_clear_count()==1);
  CHECK(d.append_demo(msg("CHANGE"),3*ns));
  renderer.render(d.snapshot(),4*ns);
  CHECK(renderer.full_clear_count()==1);
  CHECK(renderer.pixels()!=original); // a new real model value changes visible pixels
  CHECK(renderer.dirty_count()>0);
  CHECK(d.input(Input::previous)); // select the bounded max-length payload
  CHECK(d.input(Input::detail));renderer.render(d.snapshot(),4*ns);
  CHECK(renderer.full_clear_count()==1);
  CHECK(d.input(Input::scroll_down));
  renderer.render(d.snapshot(),4*ns);
  CHECK(renderer.full_clear_count()==1);
  CHECK(d.input(Input::back));renderer.render(d.snapshot(),4*ns);
  CHECK(renderer.full_clear_count()==1);
}
void test_crc_repair(){
  std::vector<uint8_t> b;
  const uint8_t raw[]={'2','N','1','2','3','4','5','6',0x15,'Q','0','1',0x02,'A',0x03};
  for(uint8_t ch:raw)b.push_back(orcsdr::acars::odd_parity_byte(ch));
  const auto crc=orcsdr::acars::crc16_kermit(b.data(),b.size());
  b.push_back(uint8_t(crc));b.push_back(uint8_t(crc>>8));
  bool repaired=false;uint8_t bits=0;
  auto valid=b;CHECK(orcsdr::acars::validate_or_repair(valid.data(),valid.size(),repaired,bits));
  CHECK(!repaired&&bits==0);
  auto one=b;one[3]^=4;
  CHECK(orcsdr::acars::validate_or_repair(one.data(),one.size(),repaired,bits));
  CHECK(repaired&&bits==1&&one==b);
  auto two=b;two[2]^=2;two[9]^=8;
  CHECK(orcsdr::acars::validate_or_repair(two.data(),two.size(),repaired,bits));
  CHECK(repaired&&bits==2&&two==b);
  auto bad=b;bad[2]^=2;bad[5]^=1;bad[9]^=8;
  CHECK(!orcsdr::acars::validate_or_repair(bad.data(),bad.size(),repaired,bits));
}
struct FrameCollector {
  unsigned hits=0;
  orcsdr::acars::Message last{};
};
void on_frame(void* p,const orcsdr::acars::Message& m){auto& f=*static_cast<FrameCollector*>(p);++f.hits;f.last=m;}
std::vector<uint8_t> make_frame(uint8_t text_start=0x02,uint8_t marker=0x03){
  const uint8_t body[]={'2','N','1','2','3','4','5','6',0x15,'Q','0','1',text_start,'A',marker};
  std::vector<uint8_t> frame;
  for(uint8_t v:body)frame.push_back(orcsdr::acars::odd_parity_byte(v));
  const auto crc=orcsdr::acars::crc16_kermit(frame.data(),frame.size());
  frame.push_back(uint8_t(crc));frame.push_back(uint8_t(crc>>8));return frame;
}
void send_frame(orcsdr::acars::FrameDecoder& decoder,const std::vector<uint8_t>& frame){
  auto send_byte=[&](uint8_t v){for(unsigned b=0;b<8;++b)decoder.push_bit((v&(1<<b))!=0);};
  for(int i=0;i<160;++i)decoder.push_bit(true);
  send_byte(orcsdr::acars::odd_parity_byte('+'));
  send_byte(orcsdr::acars::odd_parity_byte('*'));
  send_byte(0x16);send_byte(0x16);send_byte(0x01);
  for(uint8_t v:frame)send_byte(v);
  send_byte(0x7f);
}
void test_frame_status(){
  auto run=[](std::vector<uint8_t> frame)->std::pair<FrameCollector,orcsdr::acars::Stats>{
    FrameCollector c{};orcsdr::acars::FrameDecoder decoder;
    decoder.reset();decoder.set_frequency(131550000);decoder.set_callback(on_frame,&c);
    send_frame(decoder,frame);return {c,decoder.stats()};
  };
  const auto pristine=make_frame();
  auto [valid,good_stats]=run(pristine);
  CHECK(valid.hits==1&&good_stats.crc_ok==1);
  CHECK(valid.last.end_marker==0x03&&!valid.last.corrected&&valid.last.corrected_bits==0);
  auto one=pristine;one[3]^=4;
  auto [fixed_one,single_stats]=run(one);
  CHECK(fixed_one.hits==1&&fixed_one.last.corrected&&fixed_one.last.corrected_bits==1);
  CHECK(single_stats.corrected==1);
  auto two=pristine;two[2]^=2;two[9]^=8;
  auto [fixed_two,double_stats]=run(two);
  CHECK(fixed_two.hits==1&&fixed_two.last.corrected_bits==2);
  CHECK(double_stats.corrected==1);
  auto triple=pristine;triple[2]^=2;triple[5]^=1;triple[9]^=8;
  auto [rejected,bad_stats]=run(triple);
  CHECK(rejected.hits==0&&bad_stats.crc_rejected>0);
  auto [invalid_grammar,framing_stats]=run(make_frame(0x04));
  CHECK(invalid_grammar.hits==0&&framing_stats.framing_rejected>0);
  auto [continued,etb_stats]=run(make_frame(0x02,0x17));
  CHECK(continued.hits==1&&continued.last.end_marker==0x17&&etb_stats.crc_ok==1);
}
void feed_signal(Dashboard& d, size_t requested, uint64_t generation,uint32_t center=131490000){
  orcsdr::acars_selftest::Signal signal(3023,0.0,0x1234);
  std::array<uint8_t,8192> buf{};
  uint64_t idx=0;
  for(;;){const size_t got=signal.next(buf.data(),requested);if(!got)break;
    orc::linux_sdr::IqMetadata meta{};
    meta.sample_rate_sps=kInputRate;meta.frequency_hz=center;
    meta.receiver_generation=generation;meta.first_sample_index=idx;meta.monotonic_ns=10*ns;
    CHECK(d.consume(buf.data(),got,meta));idx+=got/2;
  }
  d.service();
}
void test_real_rf(){
  RadioRequest req{};CHECK(make_radio_request(kDefaultChannelHz,req));
  for(size_t chunk : {size_t(74),size_t(510),size_t(4096)}){
    Dashboard d;CHECK(d.start(req,1));feed_signal(d,chunk,1);
    auto s=d.snapshot();CHECK(s.held==1);CHECK(s.counters.valid==1);CHECK(s.counters.corrected==0);
    if(s.held){const auto& m=s.history[0].message;
      CHECK(m.frequency_hz==131550000);
      CHECK(std::strncmp(m.tail,orcsdr::acars_selftest::Signal::tail(),7)==0);
      CHECK(std::strncmp(m.label,"Q0",2)==0);
      CHECK(m.text_length==std::strlen(orcsdr::acars_selftest::Signal::text()));
      CHECK(std::memcmp(m.text,orcsdr::acars_selftest::Signal::text(),m.text_length)==0);
      CHECK(m.end_marker==0x03&&!m.corrected);
    }
    // The exact same fixture on a new receiver generation must not be relabeled.
    CHECK(d.start(req,2));feed_signal(d,4096,2);s=d.snapshot();
    CHECK(s.held==2);CHECK(s.counters.valid==2);
    CHECK(s.history[0].message.frequency_hz==kDefaultChannelHz);
    CHECK(s.history[1].message.frequency_hz==kDefaultChannelHz);
    d.stop();d.service();CHECK(!d.snapshot().active&&d.snapshot().held==2);
  }
}
void test_retune_source(){
  RadioRequest a{},b{};
  CHECK(make_radio_request(131550000,a));
  CHECK(make_radio_request(131725000,b));
  Dashboard d;
  CHECK(d.start(a,41));feed_signal(d,4096,41,a.center_hz);
  CHECK(d.snapshot().held==1);
  CHECK(d.start(b,42));feed_signal(d,4096,42,b.center_hz);
  auto s=d.snapshot();CHECK(s.held==2);
  CHECK(s.history[0].message.frequency_hz==131550000);
  CHECK(s.history[1].message.frequency_hz==131725000);
  CHECK(!d.start(a,42)); // a retune must get a new receiver generation
}
void test_discontinuity(){
  RadioRequest req{};CHECK(make_radio_request(kDefaultChannelHz,req));
  Dashboard d;CHECK(d.start(req,17));
  orcsdr::acars_selftest::Signal signal(3023,0.0,0x1234);
  std::array<uint8_t,8192> buf{};uint64_t index=0;
  // Break the RF block after sync but before its CRC: fragments cannot combine across gaps.
  while(index<26000){size_t got=signal.next(buf.data(),4096);CHECK(got>0);
    orc::linux_sdr::IqMetadata meta{};meta.frequency_hz=req.center_hz;meta.sample_rate_sps=kInputRate;
    meta.receiver_generation=17;meta.first_sample_index=index;meta.monotonic_ns=ns;
    CHECK(d.consume(buf.data(),got,meta));index+=got/2;
  }
  bool next=true;
  for(;;){size_t got=signal.next(buf.data(),4096);if(!got)break;
    orc::linux_sdr::IqMetadata meta{};meta.frequency_hz=req.center_hz;meta.sample_rate_sps=kInputRate;
    meta.receiver_generation=17;meta.first_sample_index=index;meta.monotonic_ns=2*ns;
    if(next){meta.discontinuity=true;meta.dropped_samples_before=300;next=false;}
    CHECK(d.consume(buf.data(),got,meta));index+=got/2;
  }
  d.service();CHECK(d.snapshot().held==0);
  CHECK(d.snapshot().counters.known_queue_dropped_samples==300);
  CHECK(d.snapshot().counters.stream_resets>=2);
  feed_signal(d,4096,17);CHECK(d.snapshot().held==1);
}
}
int main(){test_radio();test_ring_ui();test_render_payload();test_crc_repair();test_frame_status();test_real_rf();test_retune_source();test_discontinuity();
  if(failures){std::cerr<<failures<<" staged ACARS checks failed\n";return 1;}
  std::cout<<"Staged ACARS model, actual CU8->24k->CRC decoder, continuity, repair, UI: PASS\n";
}

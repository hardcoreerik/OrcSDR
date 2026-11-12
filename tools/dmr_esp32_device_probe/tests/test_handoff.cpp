// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr_probe/handoff.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
using namespace orcsdr::dmr::device_probe;
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return false; } } while(0)
struct IQ { float re, im; };
struct FakeReceiver {
  struct Result { std::size_t consumed; bool ok; };
  std::uint64_t processed=0;
  unsigned resets=0;
  bool fail_next=false;
  Result process(const IQ* p,std::size_t n) noexcept {
    if(!p || !n) return {0,false};
    processed+=n;
    if(fail_next){fail_next=false;return {n-1,false};}
    return {n,true};
  }
  void discontinuity() noexcept {++resets;}
};
using Ring=SpscHandoff<IQ>;
using Probe=Attachment<IQ,FakeReceiver>;
bool basic(){
  Ring q;std::array<IQ,960> data{};
  REQUIRE(q.offer(data.data(),960,48000,1)==Offer::accepted);
  REQUIRE(q.counters().queued_blocks==4 && q.counters().accepted_samples==960);
  for(unsigned i=0;i<4;i++){const auto* x=q.peek();REQUIRE(x&&x->samples==240&&x->sequence==i+1&&x->epoch==1);q.release();}
  REQUIRE(!q.peek()&&q.counters().queued_blocks==0);
  REQUIRE(q.offer(nullptr,1,48000,1)==Offer::bad_argument);
  REQUIRE(q.offer(data.data(),961,48000,1)==Offer::bad_argument);
  REQUIRE(q.offer(data.data(),240,96000,1)==Offer::wrong_rate);
  REQUIRE(q.counters().rejected_inputs==3);
  return true;
}
bool overflow_gap(){
  Probe p; std::array<IQ,960> d{};
  REQUIRE(p.offer(d.data(),960,48000,1)==Offer::accepted);
  REQUIRE(p.offer(d.data(),960,48000,1)==Offer::accepted);
  REQUIRE(p.offer(d.data(),240,48000,1)==Offer::full);
  REQUIRE(p.queue().counters().dropped_samples==240);
  for(int i=0;i<8;i++)REQUIRE(p.drain_one());
  REQUIRE(!p.drain_one());
  REQUIRE(p.offer(d.data(),240,48000,1)==Offer::accepted);
  REQUIRE(p.drain_one());
  REQUIRE(p.discontinuities()==1 && p.receiver().resets==1);
  REQUIRE(p.processed_samples()==2160 && p.queue().counters().max_queued_blocks==8);
  return true;
}
bool retune_reset(){
  Probe p;std::array<IQ,240> d{};
  REQUIRE(p.offer(d.data(),240,48000,1)==Offer::accepted);
  REQUIRE(p.offer(d.data(),240,48000,2)==Offer::accepted);
  REQUIRE(p.offer(d.data(),240,48000,2)==Offer::accepted);
  for(unsigned i=0;i<3;i++)REQUIRE(p.drain_one());
  REQUIRE(p.discontinuities()==1&&p.receiver().resets==1);
  REQUIRE(p.offer(d.data(),240,48000,3)==Offer::accepted);
  REQUIRE(p.drain_one()&&p.discontinuities()==2);
  // Explicit process errors are counted and reset before the next input.
  p.worker_receiver().fail_next=true;
  REQUIRE(p.offer(d.data(),240,48000,3)==Offer::accepted);
  REQUIRE(p.drain_one()&&p.processing_errors()==1);
  REQUIRE(p.offer(d.data(),240,48000,3)==Offer::accepted);
  REQUIRE(p.drain_one()&&p.discontinuities()==3);
  return true;
}
bool atomic_batch(){
  Ring q;std::array<IQ,960> data{};
  REQUIRE(q.offer(data.data(),960,48000,1)==Offer::accepted);
  REQUIRE(q.offer(data.data(),960,48000,1)==Offer::accepted);
  REQUIRE(q.offer(data.data(),960,48000,1)==Offer::full);
  REQUIRE(q.counters().queued_blocks==8 && q.counters().dropped_samples==960);
  for(int i=0;i<8;i++)q.release();
  REQUIRE(q.offer(data.data(),960,48000,1)==Offer::accepted);
  REQUIRE(q.peek()->sequence==13); // dropped sequence 9-12
  return true;
}
bool threaded(){
  Ring queue;std::atomic<bool> done{false}; std::atomic<bool> mismatch{false};
  std::thread producer([&](){std::array<IQ,240> block{};
    for(unsigned k=1;k<=30000;k++){
      for(auto& p:block){p.re=static_cast<float>(k);p.im=-static_cast<float>(k);}
      queue.offer(block.data(),240,48000,1);
    }
    done.store(true,std::memory_order_release);
  });
  std::uint32_t last=0;
  while(!done.load(std::memory_order_acquire)||queue.peek()){
    const auto* p=queue.peek();if(!p){std::this_thread::yield();continue;}
    if(p->sequence<=last) mismatch.store(true);
    const auto k=static_cast<std::uint32_t>(p->iq[0].re);
    for(auto& s:p->iq){if(s.re!=static_cast<float>(k)||s.im!=-static_cast<float>(k))mismatch.store(true);}
    last=p->sequence;queue.release();
  }
  producer.join();
  REQUIRE(!mismatch.load());
  const auto c=queue.counters();
  REQUIRE(c.queued_blocks==0);
  REQUIRE(c.accepted_blocks+c.dropped_blocks==30000);
  return true;
}
bool upstream_gap_and_copy(){
  Probe p;
  std::array<IQ,240> d{};
  for(auto& s:d){s.re=42;s.im=-17;}
  REQUIRE(p.offer(d.data(),240,48000,9)==Offer::accepted);
  for(auto& s:d){s.re=0;s.im=0;}
  REQUIRE(p.queue().peek()!=nullptr&&p.queue().peek()->iq[0].re==42);
  REQUIRE(p.drain_one());
  p.note_gap(240);
  REQUIRE(p.offer(d.data(),240,48000,9)==Offer::accepted);
  REQUIRE(p.drain_one());
  REQUIRE(p.discontinuities()==1);
  REQUIRE(p.queue().counters().dropped_samples==240);
  REQUIRE(p.offer(d.data(),240,96000,9)==Offer::wrong_rate);
  REQUIRE(p.offer(d.data(),240,48000,9)==Offer::accepted);
  REQUIRE(p.drain_one()&&p.discontinuities()==2);
  return true;
}
bool sustained(){
  Probe p;std::array<IQ,240> d{};
  for(unsigned k=0;k<120000U;++k){
    REQUIRE(p.offer(d.data(),240,48000,1)==Offer::accepted);
    REQUIRE(p.drain_one());
  }
  REQUIRE(p.queue().counters().queued_blocks==0);
  REQUIRE(p.queue().counters().dropped_samples==0);
  REQUIRE(p.discontinuities()==0);
  REQUIRE(p.processed_samples()==28800000ULL); // 10 minutes simulated at 48k
  return true;
}
int main(){
  for(auto fn:{basic,overflow_gap,retune_reset,atomic_batch,upstream_gap_and_copy,sustained,threaded})if(!fn())return 1;
  std::puts("PASS DMR_SPSC_HOST: 7 groups, 10min simulated stream, 30k cross-thread offers");return 0;
}

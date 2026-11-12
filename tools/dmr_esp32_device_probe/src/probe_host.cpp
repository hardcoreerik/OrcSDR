// SPDX-License-Identifier: AGPL-3.0-or-later
// Benchmark the exact native OrcSDR D0+D1 frontend+bridge, not a fake decoder.
#include "orcsdr_dmr_probe/handoff.hpp"
#include "orcsdr_dmr_probe/pattern.hpp"
#include "orcsdr_dmr/d1_bridge.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

namespace p=orcsdr::dmr::device_probe;
namespace d=orcsdr::dmr::d1;
using Receiver=d::integration::Receiver;
using Probe=p::Attachment<d::ComplexSample,Receiver>;
using Clock=std::chrono::steady_clock;
int main(int argc,char** argv) {
  int seconds=10; bool emit_log=true;bool overload=false;
  std::string log_path="dmr_probe_host.log";
  for(int i=1;i<argc;++i) {
    if(std::strcmp(argv[i],"--seconds")==0&&i+1<argc){seconds=std::atoi(argv[++i]);}
    else if(std::strcmp(argv[i],"--log")==0&&i+1<argc){log_path=argv[++i];}
    else if(std::strcmp(argv[i],"--no-log")==0){emit_log=false;}
    else if(std::strcmp(argv[i],"--overload")==0){overload=true;}
    else {std::fprintf(stderr,"usage: dmr_probe_host [--seconds 1..120] [--log filename] [--no-log] [--overload]\n");return 2;}
  }
  if(seconds<1||seconds>120)return 2;
  std::ofstream log;
  if(emit_log){log.open(log_path,std::ios::out|std::ios::trunc);if(!log){std::fprintf(stderr,"cannot write %s\n",log_path.c_str());return 2;}}
  static Probe probe;static p::Pattern waveform;
  if(!p::make_pattern(waveform))return 2;
  const auto start=Clock::now();
  std::uint64_t sum_us=0,max_us=0,worker_calls=0;
  const std::uint32_t blocks=static_cast<std::uint32_t>(seconds*200U); // 240 samples * 200 = 48k/s
  for(std::uint32_t k=0;k<blocks;++k){
    const auto* samples=waveform.data()+(k*240U)%p::kPatternSamples;
    // Epoch changes model a retune/discontinuity at 1/3 and 2/3 of run.
    const auto epoch=1U+(k>=blocks/3U)+(k>=2U*blocks/3U);
    const auto accepted=probe.offer(samples,240,48000,epoch);
    if(accepted!=p::Offer::accepted && (!overload || accepted!=p::Offer::full)){
      std::fprintf(stderr,"unexpected queue loss or invalid offer\n");return 1;
    }
    if(!overload || k%17U==0U){
      while(probe.queue().counters().queued_blocks){
        const auto t=Clock::now();
        if(!probe.drain_one())return 1;
        const auto us=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-t).count());
        sum_us+=us;if(us>max_us)max_us=us;++worker_calls;
      }
    }
  }
  while(probe.drain_one()){}
  const double wall=std::chrono::duration<double>(Clock::now()-start).count();
  const auto q=probe.queue().counters();
  const auto fs=probe.receiver().frontend_stats();
  const auto bs=probe.receiver().stats();
  char row[1600];
  const int n=std::snprintf(row,sizeof(row),
      "DMR_PROBE_HOST mode=%s sample_rate=48000 offered_samples=%u processed_samples=%llu dropped_samples=%u "
      "queue_highwater_blocks=%u discontinuities=%u processing_errors=%u wall_seconds=%.6f "
      "throughput_samples_per_second=%.2f realtime_ratio=%.3f worker_calls=%llu worker_avg_us=%.2f "
      "worker_max_us=%llu frontend_locks=%llu protocol_events=%llu validated_ids=%llu "
      "sizeof_frontend=%zu sizeof_protocol=%zu sizeof_bridge=%zu sizeof_queue=%zu sizeof_attachment=%zu "
      "device_heap=NOT_MEASURED psram=NOT_MEASURED task_stack=NOT_MEASURED\n",
      overload?"intentional-overload":"full-pipeline",q.accepted_samples,
      static_cast<unsigned long long>(probe.processed_samples()),q.dropped_samples,
      q.max_queued_blocks,probe.discontinuities(),probe.processing_errors(),wall,
      wall>0?probe.processed_samples()/wall:0.0,wall>0?(seconds/wall):0.0,
      static_cast<unsigned long long>(worker_calls),worker_calls?static_cast<double>(sum_us)/worker_calls:0.0,
      static_cast<unsigned long long>(max_us),static_cast<unsigned long long>(fs.locks),
      static_cast<unsigned long long>(bs.protocol_events),static_cast<unsigned long long>(bs.validated_identities),
      sizeof(d::Frontend),sizeof(d::Decoder),sizeof(Receiver),sizeof(Probe::Queue),sizeof(Probe));
  if(n<=0||n>=static_cast<int>(sizeof(row)))return 2;
  std::fputs(row,stdout);if(emit_log){log<<row;log.flush();if(!log)return 2;}
  if(probe.processing_errors())return 1;
  if(!overload && (q.dropped_samples!=0 || probe.processed_samples()!=static_cast<std::uint64_t>(blocks)*240U))return 1;
  if(overload && q.dropped_samples==0)return 1;
  return 0;
}

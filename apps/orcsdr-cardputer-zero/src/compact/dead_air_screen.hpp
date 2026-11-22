#pragma once
#include "compact_renderer.hpp"
#include "dead_air_model.hpp"
#include "../iq_metrics.hpp"
#include "../iq_queue.hpp"
#include <ctime>

namespace orc::zero {
class DeadAirScreen {
 public:
  void set_event_listener(orcsdr::dead_air::EventModel::EventListener listener,void* context){events_.set_listener(listener,context);}
  void storage_status(uint32_t saved,uint32_t dropped,bool failed){saved_=saved;dropped_=dropped;storage_failed_=failed;}
  void measure(const orcsdr::dead_air::RfObservation& observation) {
    events_.observe_rf(observation);
    last_=observation;
  }
  // Called on the dashboard worker with a bounded, sampled receiver block.
  bool consume(const linux_sdr::IqBlock& block) {
    const auto& meta=block.meta;
    const bool route_changed=meta.receiver_generation!=generation_ ||
      meta.frequency_hz!=last_.frequency_hz || meta.sample_rate_sps!=rate_;
    if(route_changed || meta.discontinuity || meta.dropped_samples_before)++revision_;
    generation_=meta.receiver_generation;
    rate_=meta.sample_rate_sps;
    orcsdr::dead_air::RfObservation observation{};
    observation.monotonic_ms=uint32_t(meta.monotonic_ns/1000000);
    observation.frequency_hz=meta.frequency_hz;
    const auto utc=std::time(nullptr);if(utc>=1704067200)observation.unix_time=uint32_t(utc);
    observation.configuration_revision=revision_;
    if(meta.receiver_generation && meta.frequency_hz && rate_ &&
       !meta.discontinuity && !meta.dropped_samples_before &&
       block.cu8.size()>=128 && block.cu8.size()<=32768 && !(block.cu8.size()%2)){
      observation.level_dbfs=float(orc::analyze_u8_iq(block.cu8).mean_power_dbfs);
      observation.valid=observation.settled=true;
    }
    measure(observation);
    return observation.valid;
  }
  void mark(uint32_t monotonic_ms) {
    if(last_.valid&&last_.settled)
      events_.mark_manual(monotonic_ms,last_.unix_time,last_.frequency_hz);
  }
  void render() {
    using R=CompactRenderer;
    if(!drawn_){
      frame_.fill(R::bg);
      frame_.badge(1,1,28);frame_.text(33,3,"OrcSDR DEAD AIR",R::green,2);

      frame_.keys(4,160,{{"ENTER","MARK"},{"SPACE","DASH"}});
      drawn_=true;
    }
    frame_.rect(0,35,320,119,R::bg);
    char line[52];
    if(last_.valid&&last_.settled){
      std::snprintf(line,sizeof(line),"%.3f MHZ   %.1f DBFS",last_.frequency_hz/1e6,last_.level_dbfs);
      frame_.text(4,37,line,R::white,1);
    } else frame_.text(4,37,"WAITING FOR SETTLED IQ",R::yellow,1);
    if(events_.baseline_ready()){
      std::snprintf(line,sizeof(line),"RF BASELINE %.1f DBFS",events_.rf_baseline_dbfs());
      frame_.text(4,50,line,R::cyan,1);
    } else frame_.text(4,50,"ESTABLISHING RF BASELINE",R::muted,1);
    if(!events_.count())frame_.text(4,72,"NO SESSION EVENTS",R::muted,1);
    for(size_t i=0;i<std::min<size_t>(events_.count(),5);++i){
      const auto& event=*events_.newest(i);
      std::snprintf(line,sizeof(line),"#%u %s %.3f MHZ",unsigned(event.sequence),
        event.source==orcsdr::dead_air::Source::manual?"MARK":"RF RISE",event.frequency_hz/1e6);
      frame_.text(4,72+int(i)*12,line,R::white,1);
    }
    std::snprintf(line,sizeof(line),"LOG %s SAVED %u DROP %u",storage_failed_?"ERROR":"ON",saved_,dropped_);
    frame_.text(4,142,line,storage_failed_||dropped_?R::yellow:R::cyan);
  }
  const CompactRenderer& frame() const {return frame_;}
  size_t event_count() const {return events_.count();}
 private:
  CompactRenderer frame_;
  orcsdr::dead_air::EventModel events_;
  orcsdr::dead_air::RfObservation last_{};
  uint64_t generation_=0;
  uint32_t rate_=0,revision_=0;
  bool drawn_=false,storage_failed_=false;
  uint32_t saved_=0,dropped_=0;
};
}

#include "adsb_module.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace orc::zero::adsb {
namespace {
constexpr uint64_t age(uint64_t now,uint64_t before) {return now>=before?now-before:0;}
void combine(orcsdr::adsb_rx::Stats& a,const orcsdr::adsb_rx::Stats& b) {
  a.preambles+=b.preambles;a.frames+=b.frames;a.crc_ok+=b.crc_ok;a.df17+=b.df17;
}
}
void Model::reset(uint64_t generation) {
  entries_={};unique_={};count_=unique_count_=selected_=scroll_=0;detail_=false;
  generation_=generation;counters_={};++revision_;
}
void Model::invalidate_cpr() {
  for(size_t i=0;i<count_;++i) {entries_[i].even={};entries_[i].odd={};
    entries_[i].a.has_position=false;}
  ++revision_;
}
void Model::keep_selection_visible() {
  if (!count_) {selected_=scroll_=0;detail_=false;return;}
  if(selected_>=count_)selected_=count_-1;
  if(selected_<scroll_)scroll_=selected_;
  if(selected_>=scroll_+kVisibleRows)scroll_=selected_-kVisibleRows+1;
  scroll_=std::min(scroll_,count_>kVisibleRows?count_-kVisibleRows:0u);
}
void Model::apply_verified(const orcsdr::adsb_rx::Frame& f,uint64_t now,uint64_t gen) {
  if(gen!=generation_||!now||f.icao==0||f.icao>0xffffffu||
     (f.bit_length!=112&&f.bit_length!=56)) return;
  size_t index=0;
  for(;index<count_;++index)if(entries_[index].a.icao==f.icao)break;
  if(index<count_ && now<entries_[index].a.last_seen_ns)return;
  if(index==count_) {
    // Compact exact unique-address ledger, capped rather than inventing counts.
    bool seen=false;
    for(size_t i=0;i<unique_count_;++i)if(unique_[i]==f.icao){seen=true;break;}
    if(!seen) {
      if(unique_count_<kUniqueCapacity){unique_[unique_count_++]=f.icao;}
      else counters_.unique_capped=true;
    }
    if(count_<kAircraftCapacity)index=count_++;
    else {
      index=0;
      for(size_t i=1;i<count_;++i)
        if(entries_[i].a.last_seen_ns<entries_[index].a.last_seen_ns ||
           (entries_[i].a.last_seen_ns==entries_[index].a.last_seen_ns &&
            entries_[i].a.icao<entries_[index].a.icao)) index=i;
    }
    entries_[index]={};entries_[index].a.icao=f.icao;
  }
  auto& e=entries_[index];auto& a=e.a;
  a.last_seen_ns=now;
  a.has_signal=true;a.relative_signal=f.signal;
  if(f.has_callsign){
    std::memset(a.callsign,0,sizeof(a.callsign));
    size_t n=0;while(n<8 && f.callsign[n])++n;
    std::memcpy(a.callsign,f.callsign,n);
    a.has_callsign=true;
  }
  if(f.has_altitude){a.has_altitude=true;a.altitude_ft=f.altitude_ft;}
  if(f.has_speed){a.has_speed=true;a.speed_kts=f.speed_kts;}
  if(f.has_heading){a.has_heading=true;a.heading_deg=f.heading_deg;}
  if(f.has_vertical_rate){a.has_vertical_rate=true;a.vertical_rate_fpm=f.vertical_rate_fpm;}
  if(f.has_cpr) {
    auto& target=f.cpr_odd?e.odd:e.even;
    target.frame=f;target.time_ns=now;target.generation=gen;target.valid=true;
    if(e.even.valid && e.odd.valid && e.even.generation==gen && e.odd.generation==gen &&
       age(std::max(e.even.time_ns,e.odd.time_ns),std::min(e.even.time_ns,e.odd.time_ns))<=kCprPairNs) {
      double lat=0,lon=0;
      if(orcsdr::adsb_rx::decode_global_cpr(e.even.frame,e.odd.frame,f.cpr_odd,&lat,&lon) &&
         std::isfinite(lat)&&std::isfinite(lon)&&lat>=-90&&lat<=90&&lon>=-180&&lon<=180) {
        a.latitude=lat;a.longitude=lon;a.has_position=true;
      }
    }
  }
  keep_selection_visible();++revision_;
}
void Model::expire(uint64_t now) {
  const uint32_t selected_icao=count_?entries_[selected_].a.icao:0;
  bool changed=false;
  for(size_t i=0;i<count_;) {
    if(now>=entries_[i].a.last_seen_ns && age(now,entries_[i].a.last_seen_ns)>kExpireNs) {
      for(size_t j=i+1;j<count_;++j)entries_[j-1]=entries_[j];
      entries_[--count_]={};changed=true;
    } else ++i;
  }
  if(changed){
    bool found=false;
    for(size_t i=0;i<count_;++i)if(entries_[i].a.icao==selected_icao){selected_=i;found=true;break;}
    if(!found&&selected_>=count_)selected_=count_?count_-1:0;
    keep_selection_visible();++revision_;
  }
}
void Model::navigate(Action action) {
  if(action==Action::back){if(detail_){detail_=false;++revision_;}return;}
  if(!count_)return;
  if(action==Action::open_detail){detail_=true;++revision_;return;}
  if(action==Action::up && selected_>0)--selected_;
  if(action==Action::down && selected_+1<count_)++selected_;
  keep_selection_visible();++revision_;
}
Snapshot Model::snapshot(uint64_t now,Status status,bool demo,bool receiver) const {
  Snapshot s{};s.count=count_;s.scroll=scroll_;s.selection=selected_;
  s.selected_icao=count_?entries_[selected_].a.icao:0;
  s.now_ns=now;s.status=status;s.generation=generation_;s.is_demo=demo;s.has_receiver=receiver;
  s.counters=counters_;s.counters.unique_seen=unique_count_;
  s.detail=detail_;s.revision=revision_;
  for(size_t i=0;i<count_;++i)s.aircraft[i]=entries_[i].a;
  return s;
}
bool Controller::supported_radio(uint32_t center,uint32_t rate) {
  return center==kCenterHz&&rate==kRateSps;
}
bool Controller::activate(uint64_t gen,uint64_t now,bool demo) {
  if(!gen||!now)return false;
  decoder_.reset();prior_={};model_.reset(gen);
  generation_=gen;expected_sample_=0;last_ns_=now;last_valid_ns_=0;
  known_drops_=resets_=0;active_=true;ready_=false;first_=true;demo_=demo;has_valid_=false;
  status_=demo?Status::demo:Status::connecting;
  return true;
}
void Controller::receiver_ready(uint64_t now) {
  if(active_&&!demo_&&now>=last_ns_){ready_=true;last_ns_=now;has_valid_=false;last_valid_ns_=0;status_=Status::waiting;}
}
void Controller::no_receiver(){ready_=false;status_=Status::no_receiver;reset_decoder();model_.invalidate_cpr();}
void Controller::disconnected(){ready_=false;status_=Status::disconnected;reset_decoder();model_.invalidate_cpr();}
void Controller::error(){ready_=false;status_=Status::error;reset_decoder();model_.invalidate_cpr();}
void Controller::stop(){active_=false;ready_=false;status_=Status::no_receiver;reset_decoder();model_.reset(0);}
void Controller::reset_decoder(){combine(prior_,decoder_.stats());decoder_.reset();++resets_;}
void Controller::on_frame(const orcsdr::adsb_rx::Frame& f,void* p) {
  auto& self=*static_cast<Controller*>(p);
  self.model_.apply_verified(f,self.last_ns_,self.generation_);
  self.last_valid_ns_=self.last_ns_;self.has_valid_=true;
}
Counters Controller::counters() const {
  auto s=prior_;combine(s,decoder_.stats());
  Counters c{};c.preambles=s.preambles;c.candidates=s.frames;c.crc_ok=s.crc_ok;
  c.rejected=s.frames>=s.crc_ok?s.frames-s.crc_ok:0;
  c.df17_candidates=s.df17;c.known_queue_drops=known_drops_;c.decoder_resets=resets_;
  return c;
}
Result Controller::consume_cu8(const uint8_t* cu8,size_t bytes,const orc::linux_sdr::IqMetadata& m) {
  if(!active_||!ready_||demo_)return Result::inactive;
  if(m.receiver_generation!=generation_)return Result::old_generation;
  if(!supported_radio(m.frequency_hz,m.sample_rate_sps))return Result::invalid_radio;
  if(!cu8||!bytes||(bytes&1u)||bytes>262144)return Result::invalid_data;
  if(!m.monotonic_ns||m.monotonic_ns<last_ns_||
     m.first_sample_index>std::numeric_limits<uint64_t>::max()-bytes/2) return Result::invalid_time;
  bool discontinuity=m.discontinuity||m.dropped_samples_before!=0||
                       (!first_ && m.first_sample_index!=expected_sample_);
  if(discontinuity){reset_decoder();model_.invalidate_cpr();}
  first_=false;expected_sample_=m.first_sample_index+bytes/2;last_ns_=m.monotonic_ns;
  known_drops_+=m.dropped_samples_before;
  // Decoder has 16,640 complex magnitude slots. Never pass a 32,768+-sample
  // host IQ block directly: the native Decoder silently drops excess.
  constexpr size_t kChunkComplex=4096;
  for(size_t off=0;off<bytes;off+=kChunkComplex*2)
    decoder_.process_cu8(cu8+off,std::min(bytes-off,kChunkComplex*2),&Controller::on_frame,this);
  status_=has_valid_&&age(last_ns_,last_valid_ns_)<=kStaleNs?Status::receiving:Status::waiting;
  model_.set_counters(counters());
  return Result::accepted;
}
Snapshot Controller::snapshot(uint64_t now) {
  if(now<last_ns_)now=last_ns_;
  model_.expire(now);
  auto state=status_;
  if(active_&&ready_&&!demo_){
    if(has_valid_&&age(now,last_valid_ns_)>kStaleNs)state=Status::stale;
    else if(!has_valid_)state=Status::waiting;
  }
  return model_.snapshot(now,state,demo_,ready_);
}
}  // namespace orc::zero::adsb

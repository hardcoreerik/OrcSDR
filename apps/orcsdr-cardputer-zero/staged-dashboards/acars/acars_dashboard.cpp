// SPDX-License-Identifier: AGPL-3.0-only
#include "acars_dashboard.hpp"
#include "acars_renderer.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace orc::zero::acars_dashboard {
namespace {
constexpr uint32_t kMinHz=118000000, kMaxHz=137000000;
constexpr uint32_t kStepHz=25000;
void message_error(Snapshot& s,const char* value) {
  std::snprintf(s.error,sizeof(s.error),"%s",value);
}
}
bool make_radio_request(uint32_t hz,RadioRequest& output) {
  if(hz<kMinHz||hz>kMaxHz||hz%kStepHz!=0||hz<orcsdr::acars::Frontend240k::kCarrierOffsetHz)return false;
  output={hz-uint32_t(orcsdr::acars::Frontend240k::kCarrierOffsetHz),hz,kInputRate};
  return true;
}
Dashboard::Dashboard() {
  decoder_.set_callback(decoded,this);
  (void)make_radio_request(kDefaultChannelHz,radio_);
  view_.requested_channel_hz=kDefaultChannelHz;
}
void Dashboard::bump(){++view_.revision;}
void Dashboard::reset_stream() {
  front_.reset();decoder_.reset_stream();
  resets_.fetch_add(1,std::memory_order_relaxed);
  first_=true;expected_next_=0;
}
void Dashboard::decoded(void* opaque,const orcsdr::acars::Message& msg) {
  auto& self=*static_cast<Dashboard*>(opaque);
  (void)self.push(Event{msg,self.active_generation_.load(std::memory_order_relaxed),self.block_observed_ns_});
}
bool Dashboard::push(Event event) {
  const uint32_t h=head_.load(std::memory_order_relaxed);
  if(h-tail_.load(std::memory_order_acquire)>=kPending) {
    lost_events_.fetch_add(1,std::memory_order_relaxed);return false;
  }
  pending_[h%kPending]=event;
  head_.store(h+1,std::memory_order_release);return true;
}
void Dashboard::channel_sample(void* opaque,float i,float q) {
  static_cast<Dashboard*>(opaque)->decoder_.process_channel_iq(i,q);
}
bool Dashboard::start(const RadioRequest& request,uint64_t generation) {
  RadioRequest valid{};
  if(!generation||generation<=last_started_generation_||!make_radio_request(request.channel_hz,valid)||
     valid.center_hz!=request.center_hz||request.rate_sps!=kInputRate) {
    worker_active_=false;
    worker_state_.store(uint8_t(ReceiverState::error),std::memory_order_release);
    return false;
  }
  // Every activation/retune is a new generation; stale messages cannot cross it.
  worker_active_=false;
  active_generation_.store(generation,std::memory_order_release);
  worker_channel_.store(request.channel_hz,std::memory_order_release);
  worker_center_.store(request.center_hz,std::memory_order_release);
  radio_=request;
  last_started_generation_=generation;
  reset_stream();
  decoder_.set_frequency(request.channel_hz);
  worker_active_=true;
  worker_state_.store(uint8_t(ReceiverState::receiving),std::memory_order_release);
  return true;
}
void Dashboard::stop() {
  worker_active_=false;
  reset_stream();
  active_generation_.store(0,std::memory_order_release);
  worker_state_.store(uint8_t(ReceiverState::disconnected),std::memory_order_release);
}
void Dashboard::receiver_failed(){stop();worker_state_.store(uint8_t(ReceiverState::error),std::memory_order_release);}
bool Dashboard::consume(const uint8_t* cu8,size_t bytes,const linux_sdr::IqMetadata& meta) {
  // Invalid format/tune/rate is a hard error, not a plausible "no messages" result.
  const bool valid=worker_active_&&cu8&&bytes>=2&&!(bytes&1)&&bytes<=262144 &&
    meta.sample_rate_sps==kInputRate && meta.frequency_hz==radio_.center_hz &&
    meta.receiver_generation==active_generation_.load(std::memory_order_relaxed) &&
    meta.receiver_generation!=0 && meta.first_sample_index<=UINT64_MAX-bytes/2;
  if(!valid) {
    reset_stream();worker_active_=false;
    worker_state_.store(uint8_t(ReceiverState::error),std::memory_order_release);
    return false;
  }
  if(meta.dropped_samples_before)known_drop_.fetch_add(meta.dropped_samples_before,std::memory_order_relaxed);
  if(meta.discontinuity||meta.dropped_samples_before||(!first_&&meta.first_sample_index!=expected_next_))
    reset_stream();
  first_=false;
  expected_next_=meta.first_sample_index+bytes/2;
  block_observed_ns_=meta.monotonic_ns;
  front_.process_cu8(cu8,bytes/2,channel_sample,this);
  const auto& s=decoder_.stats();
  stats_[0].store(s.crc_ok,std::memory_order_relaxed);
  stats_[1].store(s.corrected,std::memory_order_relaxed);
  stats_[2].store(s.crc_rejected,std::memory_order_relaxed);
  stats_[3].store(s.framing_rejected,std::memory_order_relaxed);
  stats_[4].store(s.overflows,std::memory_order_relaxed);
  stats_[5].store(s.sync_candidates,std::memory_order_relaxed);
  return true;
}
void Dashboard::append(const orcsdr::acars::Message& m,uint64_t ns) {
  // Callback originates only from the existing CRC-verified decoder, except explicitly tagged DEMO.
  Record r{};r.serial=next_serial_++;r.monotonic_ns=ns;r.message=m;
  r.message.text_length=std::min<uint16_t>(m.text_length,orcsdr::acars::kMaxBlockBytes);
  if(view_.held==kHistory) {
    for(size_t i=1;i<kHistory;++i)view_.history[i-1]=view_.history[i];
    if(view_.selected>0)--view_.selected;
    // If selected record was evicted, selection follows the oldest survivor.
  }else ++view_.held;
  view_.history[view_.held-1]=r;
  if(!view_.detail)view_.selected=view_.held-1;
  view_.selected_serial=view_.history[view_.selected].serial;
  bump();
}
void Dashboard::service() {
  const auto source=view_.source;
  const auto generation=active_generation_.load(std::memory_order_acquire);
  const auto radio=worker_state_.load(std::memory_order_acquire);
  if(source==Source::live) {
    const auto state=static_cast<ReceiverState>(radio)==ReceiverState::disconnected && ui_connecting_ ?
      ReceiverState::connecting : static_cast<ReceiverState>(radio);
    if(state!=ReceiverState::connecting)ui_connecting_=false;
    if(view_.receiver!=state||view_.generation!=generation||
       view_.applied_channel_hz!= (state==ReceiverState::receiving?worker_channel_.load():0)) {
      view_.receiver=state;
      view_.active=state==ReceiverState::receiving;
      view_.generation=generation;
      view_.applied_channel_hz=view_.active?worker_channel_.load():0;
      view_.applied_center_hz=view_.active?worker_center_.load():0;
      view_.applied_rate_sps=view_.active?kInputRate:0;
      if(view_.active) {
        view_.requested_channel_hz=view_.applied_channel_hz;
        view_.error[0]='\0';
      }else if(state==ReceiverState::error)message_error(view_,"IQ/RADIO ERROR");
      bump();
    }
    uint32_t t=tail_.load(std::memory_order_relaxed);
    const uint32_t h=head_.load(std::memory_order_acquire);
    while(t!=h) {
      const Event& e=pending_[t%kPending];
      if(e.generation==generation&&generation&&view_.active &&
         e.message.frequency_hz==view_.applied_channel_hz)append(e.message,e.ns);
      ++t;
    }
    tail_.store(t,std::memory_order_release);
    const uint32_t values[]={stats_[0].load(),stats_[1].load(),stats_[2].load(),stats_[3].load(),stats_[4].load(),stats_[5].load()};
    Counters next=view_.counters;
    next.valid=values[0];next.corrected=values[1];next.crc_rejected=values[2];
    next.framing_rejected=values[3];next.overflows=values[4];next.sync_candidates=values[5];
    next.known_queue_dropped_samples=known_drop_.load();next.stream_resets=resets_.load();
    next.dropped_display_events=lost_events_.load();
    if(std::memcmp(&next,&view_.counters,sizeof(next))!=0) {view_.counters=next;bump();}
  }
}
void Dashboard::set_gain(bool automatic,int tenth_db) {
  if(view_.auto_gain!=automatic||view_.gain_tenth_db!=tenth_db){view_.auto_gain=automatic;view_.gain_tenth_db=tenth_db;bump();}
}
void Dashboard::receiver_connecting(){if(view_.source==Source::live){ui_connecting_=true;view_.receiver=ReceiverState::connecting;bump();}}
bool Dashboard::input(Input action) {
  const auto old=view_.revision;
  if(action==Input::back) {if(view_.detail){view_.detail=false;view_.detail_scroll=0;bump();}}
  else if(!view_.held) return false;
  else if(action==Input::detail) {view_.detail=true;view_.detail_scroll=0;view_.history[view_.selected].unread=false;bump();}
  else if(action==Input::mark_read) {if(view_.history[view_.selected].unread){view_.history[view_.selected].unread=false;bump();}}
  else if(view_.detail) {
    if(action==Input::scroll_down) {
      const size_t max_lines=detail_line_count(view_.history[view_.selected].message);
      const size_t limit=max_lines>11?max_lines-11:0;
      if(view_.detail_scroll<limit){++view_.detail_scroll;bump();}
    }
    if(action==Input::scroll_up&&view_.detail_scroll) {--view_.detail_scroll;bump();}
  }else {
    if(action==Input::previous&&view_.selected) {--view_.selected;bump();}
    if(action==Input::next&&view_.selected+1<view_.held){++view_.selected;bump();}
    view_.selected_serial=view_.history[view_.selected].serial;
  }
  return view_.revision!=old;
}
bool Dashboard::handle_compact_key(orc::zero::Key key) {
  switch(key) {
    case orc::zero::Key::up: return input(view_.detail?Input::scroll_up:Input::previous);
    case orc::zero::Key::down: return input(view_.detail?Input::scroll_down:Input::next);
    case orc::zero::Key::enter: return view_.detail?false:input(Input::detail);
    case orc::zero::Key::escape: return input(Input::back);
    default: return false; // Space/P/Tab/F/G/T/M/S/D/Q are handled by host.
  }
}
void Dashboard::begin_demo() {
  stop();
  view_=Snapshot{};
  view_.source=Source::demo;view_.receiver=ReceiverState::receiving;view_.active=true;
  view_.applied_channel_hz=kDefaultChannelHz;view_.requested_channel_hz=kDefaultChannelHz;
  view_.applied_center_hz=kDefaultChannelHz-60000;view_.applied_rate_sps=kInputRate;
  next_serial_=1;bump();
}
bool Dashboard::append_demo(const orcsdr::acars::Message& m,uint64_t ns) {
  if(view_.source!=Source::demo||m.text_length>orcsdr::acars::kMaxBlockBytes)return false;
  append(m,ns);return true;
}
}

#include "hfdl_dashboard_model.hpp"
#include <algorithm>
#include <cstring>

namespace orcsdr::hfdl_ui {
Model::Model(){ reset(); }
void Model::reset(){
  live_=Ring{};live_.next_seq=1;
  queue_head_.store(0,std::memory_order_relaxed);
  queue_tail_.store(0,std::memory_order_relaxed);
  queue_drops_.store(0,std::memory_order_relaxed);
  receiver_state_.store(static_cast<uint8_t>(ReceiverState::idle),std::memory_order_relaxed);
  snapshot_=Snapshot{};message_offset_=0;messages_revision_=1;revision_=0;dirty_=true;
}
bool Model::post_message(const MessageRecord& msg){
  const uint32_t head=queue_head_.load(std::memory_order_relaxed);
  const uint32_t next=(head+1)%(kQueueCapacity+1);
  if(next==queue_tail_.load(std::memory_order_acquire)){
    queue_drops_.fetch_add(1,std::memory_order_relaxed);
    return false;
  }
  queue_[head]=msg;
  queue_head_.store(next,std::memory_order_release);
  return true;
}
void Model::post_receiver_state(ReceiverState state){
  receiver_state_.store(static_cast<uint8_t>(state),std::memory_order_release);
}
void Model::store(const MessageRecord& msg){
  MessageRecord& slot=live_.records[live_.head];
  slot=msg;
  slot.seq=live_.next_seq++;
  slot.source=SourceKind::live;
  slot.label[sizeof(slot.label)-1]=0;
  slot.tail[sizeof(slot.tail)-1]=0;
  slot.preview[sizeof(slot.preview)-1]=0;
  slot.text[sizeof(slot.text)-1]=0;
  slot.text_length=static_cast<uint16_t>(std::min<size_t>(slot.text_length,sizeof(slot.text)-1));
  live_.head=(live_.head+1)%kHistoryCapacity;
  if(live_.held<kHistoryCapacity)++live_.held;
  ++live_.total;++messages_revision_;dirty_=true;
}
void Model::service(){
  uint32_t tail=queue_tail_.load(std::memory_order_relaxed);
  // Reload published head each iteration, process bounded at most one full queue.
  for(size_t n=0;n<kQueueCapacity &&
      tail!=queue_head_.load(std::memory_order_acquire);++n){
    store(queue_[tail]);
    tail=(tail+1)%(kQueueCapacity+1);
    queue_tail_.store(tail,std::memory_order_release);
  }
  if(snapshot_.receiver!=static_cast<ReceiverState>(receiver_state_.load(std::memory_order_acquire)) ||
     snapshot_.queue_drops!=queue_drops_.load(std::memory_order_relaxed))
    dirty_=true;
  if(dirty_)rebuild();
}
bool Model::find_message(uint32_t seq,MessageRecord* out) const{
  if(!out || seq==0)return false;
  for(size_t i=0;i<live_.held;++i){
    const size_t idx=(live_.head+kHistoryCapacity-1-i)%kHistoryCapacity;
    if(live_.records[idx].seq==seq){*out=live_.records[idx];return true;}
  }
  return false;
}
bool Model::set_message_offset(size_t offset){
  const size_t max_offset=live_.held>0?live_.held-1:0;
  const size_t bounded=std::min(offset,max_offset);
  if(bounded==message_offset_)return false;
  message_offset_=bounded;dirty_=true;return true;
}
void Model::rebuild(){
  snapshot_.source=SourceKind::live;
  snapshot_.receiver=static_cast<ReceiverState>(receiver_state_.load(std::memory_order_acquire));
  snapshot_.queue_drops=queue_drops_.load(std::memory_order_relaxed);
  snapshot_.messages_held=static_cast<uint32_t>(live_.held);
  snapshot_.messages_total=live_.total;
  snapshot_.messages_revision=messages_revision_;
  if(message_offset_>=live_.held)message_offset_=live_.held>0?live_.held-1:0;
  snapshot_.message_offset=message_offset_;
  snapshot_.message_count=0;
  for(size_t i=0;i<kMessageRows && message_offset_+i<live_.held;++i){
    const size_t idx=(live_.head+kHistoryCapacity-1-message_offset_-i)%kHistoryCapacity;
    snapshot_.messages[snapshot_.message_count++]=live_.records[idx];
  }
  snapshot_.revision=++revision_;dirty_=false;
}
const Snapshot& Model::snapshot(){if(dirty_)rebuild();return snapshot_;}
}  // namespace orcsdr::hfdl_ui

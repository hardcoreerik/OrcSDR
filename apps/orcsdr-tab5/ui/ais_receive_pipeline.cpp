#include "ais_receive_pipeline.hpp"

namespace orcsdr::ais_rx {
void Pipeline::on_frame(const Packet& packet,void* context) {
  static_cast<Pipeline*>(context)->accept(packet);
}
void Pipeline::accept(const Packet& packet) {
  ++counters_.packets;
  ais::Message message{};
  if (ais::parse(packet.bytes,packet.bit_count,&message) != ais::ParseStatus::ok) {
    ++counters_.parse_rejected;
    return;
  }
  ++counters_.messages_decoded;
  if (message.coverage == ais::Coverage::header_only) ++counters_.messages_header_only;
  if (message.coverage != ais::Coverage::header_only) {
    if (stations_.ingest(message,block_time_ms_)) ++counters_.station_updates;
    else ++counters_.station_rejected;
  }
}
bool Pipeline::offer(const uint8_t* cu8,size_t bytes,uint32_t sample_rate,
                     uint32_t center_hz,uint32_t source_generation,uint64_t now_ms) {
  if (!source_generation || center_hz != kCenterHz || sample_rate != kInputRateHz ||
      (bytes && !cu8) || (bytes & 1u)) {
    ++counters_.input_refused;
    return false;
  }
  if (source_generation_ != source_generation) {
    receiver_.discontinuity();
    source_generation_ = source_generation;
    have_last_input_ = false;
    ++counters_.generation_changes;
  } else if (have_last_input_ &&
             (now_ms < last_input_ms_ || now_ms - last_input_ms_ > 250)) {
    receiver_.discontinuity();
    ++counters_.iq_gaps;
  }
  block_time_ms_ = now_ms;
  last_input_ms_ = now_ms;
  have_last_input_ = true;
  return receiver_.process_cu8(cu8,bytes,sample_rate,on_frame,this);
}
void Pipeline::discontinuity() {
  receiver_.discontinuity();
  have_last_input_ = false;
  ++counters_.iq_gaps;
}
void Pipeline::clear() {
  receiver_.reset();
  stations_.clear();
  source_generation_ = 0;
  have_last_input_ = false;
  last_input_ms_ = block_time_ms_ = 0;
  counters_ = {};
}
PipelineStats Pipeline::stats() const {
  PipelineStats s = counters_;
  s.rf = receiver_.stats();
  return s;
}
} // namespace orcsdr::ais_rx

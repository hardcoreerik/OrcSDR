// Loss-honest Linux CU8 -> Radio Service v1 adapter.
// Borrowed pointer remains valid only while IqBlock is unchanged.
#pragma once
#include "iq_queue.hpp"
#include "orc_radio_service_v1.h"
#include <stdexcept>
namespace orc::linux_sdr {
inline orc_radio_v1_frame_t describe_iq_v1(
    const IqBlock& block,const orc_radio_v1_identity_t& identity,
    std::uint64_t sequence,bool upstream_loss_known=false) {
  if(identity.struct_size!=sizeof(identity) ||
     identity.generation!=block.meta.receiver_generation ||
     block.cu8.empty()||(block.cu8.size()&1u)||
     !block.meta.sample_rate_sps||!block.meta.frequency_hz)
    throw std::invalid_argument("invalid IQ block or receiver identity");
  orc_radio_v1_frame_t f{};
  f.struct_size=sizeof(f);
  f.identity=identity;
  f.sequence=sequence;
  f.time.struct_size=sizeof(f.time);
  f.time.monotonic_ns=block.meta.monotonic_ns;
  f.time.utc_valid=0; // USB read-completion timestamp; never invent ADC UTC
  f.kind=ORC_RADIO_V1_FRAME_IQ;
  f.format=ORC_RADIO_V1_CU8;
  f.center_frequency_hz=block.meta.frequency_hz;
  f.sample_rate_sps=block.meta.sample_rate_sps;
  f.first_sample_index=block.meta.first_sample_index;
  f.samples_or_frames=block.cu8.size()/2;
  // Queue loss != all USB loss. Do not advertise fully measured drop counts.
  f.dropped_known=upstream_loss_known?1:0;
  f.dropped_since_previous=upstream_loss_known?block.meta.dropped_samples_before:0;
  f.bytes=block.cu8.data();
  f.byte_count=block.cu8.size();
  return f;
}

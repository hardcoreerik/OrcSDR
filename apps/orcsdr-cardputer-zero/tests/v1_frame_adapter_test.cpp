#include "v1_frame_adapter.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
int main() {
  using namespace orc::linux_sdr;
  IqBlock block;
  block.cu8={127,127,128,128,129,129,130,130};
  block.meta.frequency_hz=131490000;
  block.meta.sample_rate_sps=240000;
  block.meta.receiver_generation=7;
  block.meta.first_sample_index=123456;
  block.meta.monotonic_ns=987654321;
  block.meta.dropped_samples_before=6;
  orc_radio_v1_identity_t identity{};
  identity.struct_size=sizeof(identity);
  identity.generation=7;
  std::strcpy(identity.host_id,"fixture-host");
  std::strcpy(identity.receiver_id,"fixture-receiver");
  const auto frame=describe_iq_v1(block,identity,4);
  if(frame.kind!=ORC_RADIO_V1_FRAME_IQ||frame.format!=ORC_RADIO_V1_CU8||
     frame.samples_or_frames!=4||frame.byte_count!=8||
     frame.bytes!=block.cu8.data()||frame.first_sample_index!=123456||
     frame.time.utc_valid!=0||frame.dropped_known!=0||
     frame.center_frequency_hz!=131490000||frame.identity.generation!=7)
    return 1;
  const auto measured=describe_iq_v1(block,identity,5,true);
  if(!measured.dropped_known||measured.dropped_since_previous!=6)return 2;
  identity.generation=8;
  try {(void)describe_iq_v1(block,identity,6);return 3;}
  catch(const std::invalid_argument&){}
  std::cout<<"Linux CU8 metadata to v1 with honest unknown USB loss: PASS\n";
  return 0;
}

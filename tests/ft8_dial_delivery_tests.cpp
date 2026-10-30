#include "../apps/orcsdr-tab5/ui/ft8_dial_delivery.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace orcsdr::ft8;
int main() {
  dial_preview::Delivery delivery;
  orc::projection::Peer peer;
  Decode d{}; d.dial_hz=14074000; d.kind=DecodeKind::cq;
  std::strcpy(d.callsign,"W1AW"); std::strcpy(d.grid,"FN31");
  std::strcpy(d.message,"CQ W1AW FN31");
  d.flags=decode_flag_snr_unavailable;
  unsigned frames=0; uint8_t summary[64]{}; uint32_t id=0;
  auto send=[&](const uint8_t* wire) {
    ++frames; assert(wire[6]==2 && orc::get32(wire+8)==123);
    assert(orc::get32(wire+60)==orc::crc32(wire,60));
    if(wire[5]==17){std::memcpy(summary,wire,64); id=orc::get32(wire+12);}
    else assert(wire[5]==18 && orc::get32(wire+12)==id);
    return true;
  };
  assert(!delivery.poll(10,123,true,send)); // no decode, no synthetic record
  delivery.observe(d,10);
  assert(!delivery.poll(11,123,peer.enabled(11),send)); // old/missing peer
  orc::Packet h{}; h.role=orc::Role::dial;h.type=orc::Type::heartbeat;
  h.sender=1;h.sequence=1;orc::projection::request(h);peer.observe(h,11);
  assert(delivery.poll(11,123,peer.enabled(11),send));assert(frames==2 && id==1);
  assert(summary[54]==dial_preview::grid_associated); // no SNR or UTC invented
  assert(!delivery.poll(12,123,true,send));
  assert(delivery.poll(1511,123,true,send));assert(id==1); // retry same identity
  for(int i=0;i<10000;++i)delivery.observe(d,1512); // constant memory, latest only
  assert(!delivery.poll(1512,123,true,send));
  assert(delivery.poll(3011,123,true,send));assert(id==10001);
  assert(!delivery.poll(60000,123,peer.enabled(60000),send)); // expired negotiation
  assert(!delivery.poll(61512,123,true,send)); // expired source record

  delivery.observe(d,UINT32_MAX-10);
  assert(delivery.poll(1500,123,true,send)); // millis rollover
  // A partial send is retried with the same record, with no text orphan queued.
  unsigned refused=0;
  assert(!delivery.poll(3000,123,true,[&](const uint8_t*){++refused;return false;}));
  assert(refused==1);assert(delivery.poll(4500,123,true,send));
  uint8_t b[64]{},t[64]{};
  std::strcpy(d.grid,"FN32"); // does not match the actual CQ message
  assert(dial_preview::encode(d,123,1,b,t));assert(!(b[54]&dial_preview::grid_associated));
  d.callsign[0]=0;d.grid[0]=0;d.kind=DecodeKind::free_text;
  std::memset(d.message,'A',47);d.message[47]=0;
  assert(dial_preview::encode(d,123,1,b,t));assert(b[54]&dial_preview::message_truncated);
  assert(t[59]==0 && b[28]==0 && b[44]==0);
  std::memset(d.grid,'A',sizeof(d.grid));
  assert(!dial_preview::encode(d,123,1,b,t));
  d.grid[0]=0;std::memset(d.message,'A',sizeof(d.message));
  assert(!dial_preview::encode(d,123,1,b,t));
  std::puts("ft8_dial_delivery_tests PASS");
}

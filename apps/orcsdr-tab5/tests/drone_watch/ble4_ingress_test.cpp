#include "drone_watch_ble4_ingress.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace orcsdr::drone_watch::rid;
int main() {
  Ble4Ingress ingress;
  ingress.reset();
  std::array<uint8_t,31> ad{};
  ad[0]=0x1e; ad[1]=0x16; ad[2]=0xfa; ad[3]=0xff;
  ad[4]=0x0d; ad[5]=0xfe; ad[6]=0x02; ad[7]=0x12;
  std::memcpy(ad.data()+8,"ORC-TEST-001",12);
  CaptureMeta live{Transport::bluetooth4,2000,11};
  assert(ingress.accept(ad.data(),ad.size(),live)==Ble4IngressResult::decoded);
  StoredEvent evt{};
  assert(ingress.observations().newest(0,evt));
  assert(evt.capture.source_id==11 && evt.capture.received_ms==2000);
  assert(evt.capture.advertising_counter_present && evt.capture.advertising_counter==0xfe);
  assert(!evt.test_data && evt.observation.claimed_id_present);
  assert(std::strcmp(evt.observation.claimed_id,"ORC-TEST-001")==0);

  // Different capture source and a replay flag must not become "live" data.
  CaptureMeta replay{Transport::bluetooth4,3000,22,true};
  ad[5]=0xff;
  assert(ingress.accept(ad.data(),ad.size(),replay)==Ble4IngressResult::decoded);
  assert(ingress.observations().newest(0,evt));
  assert(evt.capture.source_id==22 && evt.test_data);
  assert(evt.capture.advertising_counter==0xff);
  assert(ingress.observations().newest(1,evt));
  assert(evt.capture.source_id==11 && !evt.test_data);

  auto unrelated=ad;
  unrelated[2]=0x01; // other BLE service
  assert(ingress.accept(unrelated.data(),unrelated.size(),live)==
         Ble4IngressResult::unrelated_advertisement);
  assert(ingress.observations().count()==2);

  assert(ingress.accept(ad.data(),12,live)==
         Ble4IngressResult::malformed_advertisement);
  assert(ingress.observations().count()==2);

  auto invalid=ad;
  invalid[6]=0xE2; // invalid OpenDroneID application-message family
  assert(ingress.accept(invalid.data(),invalid.size(),live)==
         Ble4IngressResult::invalid_remote_id);
  assert(ingress.observations().count()==2);

  CaptureMeta false_origin{Transport::wifi_beacon,9000,9};
  assert(ingress.accept(ad.data(),ad.size(),false_origin)==
         Ble4IngressResult::wrong_transport);
  assert(ingress.observations().count()==2);

  const auto& stats=ingress.counters();
  assert(stats.reports_received==6);
  assert(stats.decoded_reports==2);
  assert(stats.unrelated_reports==1);
  assert(stats.malformed_advertisements==1);
  assert(stats.invalid_remote_id==1);
  assert(stats.wrong_transport==1);
  ingress.reset();
  assert(ingress.observations().count()==0);
  assert(ingress.counters().reports_received==0);
  std::cout << "BLE4 ingress PASS: end-to-end AD extraction, decode, source, replay and reject counters\n";
}

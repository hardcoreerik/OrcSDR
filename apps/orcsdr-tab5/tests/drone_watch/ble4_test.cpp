#include "drone_watch_ble4.hpp"
#include "drone_watch_rid.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
using namespace orcsdr::drone_watch::rid;
int main() {
  // Byte layout copied as a fixture (not source code) from the official
  // opendroneid/transmitter-linux bluetooth.c, pinned by blob SHA in review.
  // HCI Set Advertising Data has a preceding length 0x1F; AD data starts here.
  std::array<uint8_t,31> ad{};
  ad[0]=0x1e; ad[1]=0x16; ad[2]=0xfa; ad[3]=0xff;
  ad[4]=0x0d; ad[5]=0x7c; // application code then rolling counter
  ad[6]=0x02; ad[7]=0x12;
  std::memcpy(ad.data()+8,"ORC-BLE-TEST",12);
  Ble4Message msg{};
  assert(extract_ble4_advertising_data(ad.data(),ad.size(),msg)==Ble4FrameStatus::matched);
  assert(msg.advertising_counter==0x7c);
  assert(std::memcmp(msg.encoded,ad.data()+6,25)==0);
  Batch out{};
  const CaptureMeta hw{Transport::bluetooth4,500,12};
  assert(decode(msg.encoded,sizeof(msg.encoded),hw,out)==ParseStatus::ok);
  assert(out.count==1 && out.messages[0].kind==Kind::basic_id);
  assert(out.messages[0].claimed_id_present);
  assert(std::strcmp(out.messages[0].claimed_id,"ORC-BLE-TEST")==0);

  assert(extract_ble4_advertising_data(nullptr,31,msg)==Ble4FrameStatus::invalid_argument);
  assert(msg.encoded[0]==0);
  assert(extract_ble4_advertising_data(nullptr,0,msg)==Ble4FrameStatus::no_remote_id);
  assert(extract_ble4_advertising_data(ad.data(),32,msg)==Ble4FrameStatus::invalid_length);
  assert(extract_ble4_advertising_data(ad.data(),30,msg)==Ble4FrameStatus::malformed_ad);
  auto notrid=ad;
  notrid[2]=0xff; // not UUID FF FA
  assert(extract_ble4_advertising_data(notrid.data(),notrid.size(),msg)==Ble4FrameStatus::no_remote_id);
  notrid=ad; notrid[4]=0x7a; // other ASTM application, not ODID
  assert(extract_ble4_advertising_data(notrid.data(),notrid.size(),msg)==Ble4FrameStatus::no_remote_id);
  notrid=ad; notrid[1]=0xff; // manufacturer-specific rather than service data
  assert(extract_ble4_advertising_data(notrid.data(),notrid.size(),msg)==Ble4FrameStatus::no_remote_id);
  notrid=ad; notrid[0]=0x1d; // exact ODID frame is 31 bytes, shorter envelope invalid
  assert(extract_ble4_advertising_data(notrid.data(),notrid.size(),msg)==Ble4FrameStatus::malformed_remote_id);
  std::array<uint8_t,31> tiny{};
  tiny[0]=0x04; tiny[1]=0x16; tiny[2]=0xfa; tiny[3]=0xff; tiny[4]=0x0d;
  assert(extract_ble4_advertising_data(tiny.data(),5,msg)==Ble4FrameStatus::malformed_remote_id);
  tiny={}; tiny[0]=0x02; tiny[1]=0x01; tiny[2]=0x04; // ordinary BLE flag field
  assert(extract_ble4_advertising_data(tiny.data(),3,msg)==Ble4FrameStatus::no_remote_id);
  assert(extract_ble4_advertising_data(tiny.data(),2,msg)==Ble4FrameStatus::malformed_ad);
  std::mt19937 rng(0xB1E4);
  for (int i=0;i<4000;++i) {
    std::array<uint8_t,31> bytes{};
    const size_t n=static_cast<size_t>(rng()%32);
    for(size_t j=0;j<n;++j) bytes[j]=static_cast<uint8_t>(rng());
    const auto status=extract_ble4_advertising_data(bytes.data(),n,msg);
    if(status!=Ble4FrameStatus::matched) {
      assert(msg.encoded[0]==0 && msg.advertising_counter==0);
    } else {
      // A matched service-data frame may still have malformed 25-byte ODID.
      // The core decoder has a separate validation gate.
      assert(n==31);
    }
  }
  std::cout << "BLE4 ASTM envelope PASS: upstream layout, unsupported/malformed, 4000 bounded inputs\n";
}

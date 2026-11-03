#include "packet_radio_ui.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
  packet_radio::Packet packet{};
  packet.frequency_hz=906875000;packet.received_us=12345000;
  packet.interpreted=true;packet.mesh.sender=0x12345678;packet.mesh.destination=0xffffffff;
  packet.mesh.packet_id=42;packet.mesh.port=1;packet.mesh.encrypted=true;
  packet.rssi_dbm=-91;packet.snr_db=7;
  std::strcpy(packet.mesh.text,"OrcSDR SX1262 native UI test");
  auto event=packet_radio::ui_event(packet);
  assert(event.source==orcsdr::lora::PacketSource::sx1262&&event.frequency_hz==906875000);
  assert(event.received_ms==12345&&event.received_utc==0&&event.verified);
  assert(event.sender==packet.mesh.sender&&event.packet_id==42&&event.destination==0xffffffff);
  assert(event.signal_tenths==-910&&event.snr_tenths==70&&!std::strcmp(event.text,packet.mesh.text));
  packet.interpreted=false;event=packet_radio::ui_event(packet);assert(!event.verified&&!event.text[0]);
  packet.interpreted=true;packet.mesh.port=0;std::strcpy(packet.mesh.text,"ENCRYPTED FRAME");
  event=packet_radio::ui_event(packet);assert(event.verified&&event.encrypted&&!event.text[0]);
  packet.mesh.port=1;event=packet_radio::ui_event(packet);assert(!std::strcmp(event.text,"ENCRYPTED FRAME"));
  std::memset(packet.mesh.text,'x',sizeof(packet.mesh.text));
  event=packet_radio::ui_event(packet);assert(std::strlen(event.text)==sizeof(event.text)-1);
  packet.rssi_dbm=INT16_MIN;packet.snr_db=INT16_MAX;
  event=packet_radio::ui_event(packet);assert(event.signal_tenths==INT16_MIN&&event.snr_tenths==INT16_MAX-1);
  packet.mesh.sender=0;event=packet_radio::ui_event(packet);assert(!event.verified&&!event.text[0]);
  puts("hardware packet -> shared UI event identity/timing/signal/text trust PASS");
}

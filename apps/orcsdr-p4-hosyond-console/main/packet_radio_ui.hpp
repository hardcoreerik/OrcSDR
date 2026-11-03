#pragma once
#include <algorithm>
#include <cstdio>
#include "packet_radio_service.hpp"
#include "lora_dashboard.hpp"

namespace packet_radio {
// A CRC-accepted hardware packet is distinct from a successfully interpreted
// Meshtastic payload. Never present uninterpreted bytes/sentinels as message text.
inline orcsdr::lora::Event ui_event(const Packet& packet) {
  orcsdr::lora::Event event{};
  event.source=orcsdr::lora::PacketSource::sx1262;
  event.frequency_hz=packet.frequency_hz;
  event.received_ms=packet.received_us>0?static_cast<uint32_t>(packet.received_us/1000):0;
  event.verified=packet.interpreted && packet.mesh.sender!=0;
  event.sender=packet.mesh.sender;event.destination=packet.mesh.destination;
  event.packet_id=packet.mesh.packet_id;event.port=packet.mesh.port;
  event.encrypted=packet.mesh.encrypted;
  event.latitude_e7=packet.mesh.latitude_e7;event.longitude_e7=packet.mesh.longitude_e7;
  event.signal_tenths=static_cast<int16_t>(std::clamp<int32_t>(int32_t(packet.rssi_dbm)*10,INT16_MIN,INT16_MAX-1));
  event.snr_tenths=static_cast<int16_t>(std::clamp<int32_t>(int32_t(packet.snr_db)*10,INT16_MIN,INT16_MAX-1));
  if(event.verified && !(event.encrypted && !event.port))
    std::snprintf(event.text,sizeof(event.text),"%.*s",int(sizeof(packet.mesh.text)),packet.mesh.text);
  return event;
}
} // namespace packet_radio

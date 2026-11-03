#pragma once
#include <cstdint>
#include "esp_err.h"
#include "lora_native_decoder.hpp"
namespace packet_radio {
struct Packet {
  orcsdr::lora_native::Packet mesh{};
  uint32_t sequence=0,generation=0,frequency_hz=0;
  int64_t received_us=0;
  int16_t rssi_dbm=0,snr_db=0;
  uint8_t length=0;
  bool interpreted=false,dio1_high=false;
};
struct Status {
  bool supported=false,started=false,requested=false,starting=false,receiving=false;
  bool external_antenna=false,detected=false,clock_checked=false;
  uint32_t generation=0,requested_hz=0,frequency_hz=0,packets=0,rejected=0,dropped=0;
  uint32_t stale_discarded=0;
  esp_err_t last_error=ESP_OK;
  Packet latest{};
};
bool start(); // process-lifetime worker; initially RF-off
bool receive_available(); // advisory UI availability; requests validate again
bool request_receive(uint32_t frequency_hz,bool external_antenna);
bool request_stop(); // asynchronous; status().receiving clears after hardware cleanup
Status status();
// Sole UI consumer. Discards packets from cancelled/older receive generations;
// latest retains the last valid packet with its original frequency/generation.
bool take_packet(Packet* packet);
} // namespace packet_radio

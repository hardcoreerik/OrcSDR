#include "control/protocol.hpp"
#include "state.hpp"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/queue.h>
#include <cstring>

// Bench-only receiver. Type p in the serial monitor to open pairing for 30 seconds.
namespace {
struct Incoming { uint8_t mac[6]; uint8_t data[orc::packet_size]; };
QueueHandle_t inbox;
uint8_t dial_mac[6]{};
bool paired = false;
uint32_t pairing_until = 0, last_sequence = 0, last_sender = 0, next_sequence = 1;
uint32_t device_id = 0;
orc::RadioState state;
const uint8_t broadcast[6] = {255,255,255,255,255,255};

void receive(const uint8_t* mac, const uint8_t* data, int size) {
  if (size != int(orc::packet_size)) return;
  Incoming item{}; std::memcpy(item.mac, mac, 6);
  std::memcpy(item.data, data, orc::packet_size);
  xQueueSend(inbox, &item, 0);
}
void add_peer(const uint8_t* mac) {
  if (paired) esp_now_del_peer(dial_mac);
  std::memcpy(dial_mac, mac, 6);
  esp_now_peer_info_t peer{}; std::memcpy(peer.peer_addr, mac, 6);
  peer.ifidx = WIFI_IF_STA; peer.channel = 0; esp_now_add_peer(&peer);
  paired = true;
}
void reply(orc::Type type, const uint8_t* mac, uint32_t ack = 0) {
  orc::Packet p;
  p.role = orc::Role::receiver; p.type = type; p.sequence = next_sequence++;
  p.sender = device_id;
  p.ack = ack; p.frequency_hz = state.frequency_hz; p.step_hz = state.step_hz;
  p.mode = state.mode; p.gain_tenth_db = state.gain_tenth_db;
  p.squelch = state.squelch; p.volume = state.volume; p.flags = 0;
  p.dashboard = uint8_t(state.dashboard);
  uint8_t wire[orc::packet_size]; orc::encode(p, wire);
  esp_now_send(mac, wire, sizeof wire);
}
void handle(const Incoming& item) {
  orc::Packet p;
  if (!orc::decode(item.data, sizeof item.data, p) || p.role != orc::Role::dial) return;
  const bool known = paired && std::memcmp(item.mac, dial_mac, 6) == 0;
  if (p.type == orc::Type::hello) {
    if (known || millis() < pairing_until) reply(orc::Type::hello, known ? dial_mac : broadcast);
    return;
  }
  if (p.type == orc::Type::pair_request && millis() < pairing_until) {
    add_peer(item.mac);
    Preferences prefs; prefs.begin("orcdial", false);
    prefs.putBytes("peer", dial_mac, 6); prefs.end();
    last_sequence = 0; last_sender = p.sender; pairing_until = 0;
    reply(orc::Type::pair_ack, dial_mac, p.sequence);
    Serial.println("PAIR_OK"); return;
  }
  if (!known) return;
  if (p.type == orc::Type::heartbeat || p.type == orc::Type::request_state) {
    reply(orc::Type::radio_state, dial_mac, p.sequence); return;
  }
  if (p.sender != last_sender) { last_sender = p.sender; last_sequence = 0; }
  if (!orc::newer_sequence(p.sequence, last_sequence)) {
    reply(orc::Type::radio_state, dial_mac, p.sequence); return;
  }
  switch (p.type) {
    case orc::Type::tune_relative:
      state.frequency_hz = orc::clamp_frequency(int64_t(state.frequency_hz) + p.value); break;
    case orc::Type::tune_absolute: state.frequency_hz = orc::clamp_frequency(p.value); break;
    case orc::Type::set_step: if (p.value > 0 && p.value <= 1000000) state.step_hz = p.value; break;
    case orc::Type::set_mode: if (p.value >= 1 && p.value <= 3) state.mode = p.value; break;
    case orc::Type::set_gain: state.gain_tenth_db = constrain(p.value, 0, 500); break;
    case orc::Type::set_volume: state.volume = constrain(p.value, 0, 100); break;
    case orc::Type::set_squelch: state.squelch = constrain(p.value, 0, 100); break;
    case orc::Type::set_dashboard:
      if (p.value < 0 || p.value > 16 || !orc::valid_dashboard(uint8_t(p.value))) return;
      state.dashboard = orc::Dashboard(p.value); break;
    default: return;
  }
  last_sequence = p.sequence;
  Serial.printf("RX seq=%lu type=%u value=%ld freq=%lu\n",
                (unsigned long)p.sequence, unsigned(p.type), long(p.value),
                (unsigned long)state.frequency_hz);
  reply(orc::Type::radio_state, dial_mac, p.sequence);
}
}
void setup() {
  Serial.begin(115200); WiFi.mode(WIFI_STA); WiFi.disconnect(false, false);
  uint8_t mac[6]; esp_wifi_get_mac(WIFI_IF_STA, mac); device_id = orc::get32(mac + 2);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  inbox = xQueueCreate(8, sizeof(Incoming));
  if (esp_now_init() != ESP_OK) { Serial.println("ESPNOW_INIT_FAILED"); return; }
  esp_now_register_recv_cb(receive);
  esp_now_peer_info_t peer{}; std::memcpy(peer.peer_addr, broadcast, 6);
  peer.ifidx = WIFI_IF_STA; esp_now_add_peer(&peer);
  Preferences prefs; prefs.begin("orcdial", true);
  if (prefs.getBytesLength("peer") == 6 && prefs.getBytes("peer", dial_mac, 6) == 6) {
    uint8_t saved[6]; std::memcpy(saved, dial_mac, 6); paired = false; add_peer(saved);
  }
  prefs.end();
  Serial.println("ORCDIAL_RECEIVER_READY channel=1; type p to pair");
}
void loop() {
  if (Serial.available() && Serial.read() == 'p') {
    pairing_until = millis() + 30000; Serial.println("PAIR_WINDOW_OPEN");
  }
  Incoming item;
  while (inbox && xQueueReceive(inbox, &item, 0) == pdTRUE) handle(item);
  delay(5);
}

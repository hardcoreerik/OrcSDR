#include "link.hpp"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <cstring>

namespace orc {
Link* Link::active_ = nullptr;
static const uint8_t broadcast[6] = {255,255,255,255,255,255};

bool Link::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  if (esp_now_init() != ESP_OK) return false;
  queue_ = xQueueCreate(8, sizeof(Incoming));
  if (!queue_) return false;
  active_ = this;
  esp_now_register_recv_cb(receive);
  uint8_t mac[6]; esp_wifi_get_mac(WIFI_IF_STA, mac);
  device_id_ = esp_random(); // per-boot session ID; MAC remains the paired identity
  Serial.printf("ORCDIAL_MAC %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  esp_now_peer_info_t info{};
  std::memcpy(info.peer_addr, broadcast, 6); info.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&info);
  Preferences saved; saved.begin("orcdial", false);
  has_peer_ = saved.getBytesLength("peer") == 6 && saved.getBytes("peer", peer_mac_, 6) == 6;
  saved.end();
  if (has_peer_) peer(peer_mac_);
  Serial.println("ESPNOW_INIT_OK");
  return true;
}
void Link::receive(const uint8_t* mac, const uint8_t* data, int size) {
  if (!active_ || size != int(packet_size)) return;
  Incoming item{}; std::memcpy(item.mac, mac, 6);
  std::memcpy(item.data, data, packet_size); item.size = size;
  xQueueSend(active_->queue_, &item, 0);
}
void Link::peer(const uint8_t* mac) {
  if (has_peer_) esp_now_del_peer(peer_mac_);
  std::memcpy(peer_mac_, mac, 6);
  esp_now_peer_info_t info{};
  std::memcpy(info.peer_addr, mac, 6); info.ifidx = WIFI_IF_STA;
  info.channel = 0;
  esp_now_add_peer(&info);
  has_peer_ = true;
}
bool Link::send(Type type, int32_t value, uint32_t sequence) {
  Packet p;
  p.type = type; p.role = Role::dial; p.sender = device_id_;
  p.sequence = sequence ? sequence : next_sequence_++;
  p.value = value;
  p.frequency_hz = state_.frequency_hz; p.step_hz = state_.step_hz;
  p.dashboard = uint8_t(state_.dashboard);
  uint8_t data[packet_size]; encode(p, data);
  const uint8_t* dest = (type == Type::hello) ? broadcast : peer_mac_;
  const bool sent = esp_now_send(dest, data, packet_size) == ESP_OK;
  if (sent &&
      type != Type::hello && type != Type::heartbeat && type != Type::pair_request) {
    pending_ = p; pending_sequence_ = p.sequence; pending_ms_ = millis(); retries_ = 0;
    Serial.printf("TX seq=%lu type=%u value=%ld\n", (unsigned long)p.sequence, unsigned(type), long(value));
  }
  return sent;
}
void Link::start_pairing() {
  pairing_ = true; connected_ = false; scan_channel_ = 1;
  Serial.println("PAIR_SEARCH");
}
bool Link::command(Type type, int32_t value) {
  if (!connected_ || pending_sequence_) return false;
  return send(type, value);
}
void Link::handle(const Incoming& incoming) {
  Packet p;
  if (!decode(incoming.data, incoming.size, p) || p.role != Role::receiver) return;
  bool known = has_peer_ && std::memcmp(incoming.mac, peer_mac_, 6) == 0;
  if (pairing_ && p.type == Type::hello) {
    Serial.println("PEER_FOUND");
    peer(incoming.mac); send(Type::pair_request); return;
  }
  if (!known && !pairing_) return;
  if (pairing_ && p.type == Type::pair_ack && known) {
    Preferences saved; saved.begin("orcdial", false);
    saved.putBytes("peer", peer_mac_, 6); saved.end();
    pairing_ = false; connected_ = false; last_rx_ms_ = millis();
    Serial.println("PAIR_OK");
    send(Type::request_state); return;
  }
  if (!known) return;
  if (p.type == Type::hello) { send(Type::request_state); return; }
  last_rx_ms_ = millis();
  if (p.type == Type::radio_state) {
    if (p.frequency_hz < 24000 || p.frequency_hz > 1766000000 || !valid_dashboard(p.dashboard)) return;
    connected_ = true;
    state_.frequency_hz = p.frequency_hz; state_.step_hz = p.step_hz;
    state_.gain_tenth_db = p.gain_tenth_db; state_.squelch = p.squelch;
    state_.signal_dbm = p.signal_dbm; state_.signal_valid = p.flags & 1;
    state_.mode = p.mode; state_.volume = p.volume;
    state_.dashboard = Dashboard(p.dashboard);
    if (p.ack == pending_sequence_) { last_ack_ = p.ack; pending_sequence_ = 0; }
    Serial.printf("RX seq=%lu type=RADIO_STATE freq=%lu\n", (unsigned long)p.sequence, (unsigned long)p.frequency_hz);
  }
}
void Link::poll() {
  if (!queue_) return;
  Incoming incoming;
  while (xQueueReceive(queue_, &incoming, 0) == pdTRUE) handle(incoming);
  const uint32_t now = millis();
  if (pending_sequence_ && now - pending_ms_ > 650) {
    if (++retries_ > 2) {
      Serial.printf("ACK_TIMEOUT seq=%lu\n", (unsigned long)pending_sequence_);
      pending_sequence_ = 0; connected_ = false;
    } else {
      uint8_t data[packet_size]; encode(pending_, data);
      esp_now_send(peer_mac_, data, packet_size); pending_ms_ = now;
    }
  }
  if (connected_ && now - last_rx_ms_ > 3000) {
    connected_ = false; Serial.println("LINK_LOST");
  }
  if (pairing_ || !connected_) {
    if (now - scan_ms_ >= 350) {
      esp_wifi_set_channel(scan_channel_, WIFI_SECOND_CHAN_NONE);
      send(Type::hello); scan_channel_ = scan_channel_ == 11 ? 1 : scan_channel_ + 1;
      scan_ms_ = now;
    }
  } else if (now - last_tx_ms_ > 1000) {
    send(Type::heartbeat); last_tx_ms_ = now;
  }
}
} // namespace orc

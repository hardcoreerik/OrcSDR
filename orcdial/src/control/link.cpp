#include "link.hpp"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <cstring>

namespace orc {
Link* Link::active_ = nullptr;
volatile uint8_t Link::channel_ = 1;
volatile uint8_t Link::trace_mode_ = 0;
volatile uint32_t Link::last_unicast_ok_ms_ = 0;
volatile uint32_t Link::set_channel_errors_ = 0;
Link::RfEvent Link::ring_[Link::rf_ring_size];
volatile uint32_t Link::ring_head_ = 0, Link::ring_tail_ = 0;
portMUX_TYPE Link::ring_lock_ = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t Link::tx_accepted_ = 0, Link::tx_refused_ = 0, Link::cb_ok_ = 0, Link::cb_fail_ = 0, Link::rx_frames_ = 0;
volatile uint32_t Link::tx_by_channel_[14] = {}, Link::rx_by_channel_[14] = {};
static const uint8_t broadcast[6] = {255,255,255,255,255,255};
static const char* const rf_names[] = {"", "tx", "tx_refused", "cb_ok", "cb_fail", "rx", "scan"};

void Link::note(RfKind kind) {
  const uint8_t ch = channel_ < 14 ? channel_ : 0;
  portENTER_CRITICAL(&ring_lock_);
  ring_[ring_head_ % rf_ring_size] = {millis(), uint8_t(kind), ch};
  ++ring_head_;
  if (ring_head_ - ring_tail_ > rf_ring_size) ring_tail_ = ring_head_ - rf_ring_size;  // drop oldest
  portEXIT_CRITICAL(&ring_lock_);
}
void Link::sent(const uint8_t* mac, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    ++cb_ok_; note(rf_cb_ok);
    // Broadcast "success" only means the frame left the radio; a unicast ACK means the peer heard it.
    if (mac && std::memcmp(mac, broadcast, 6) != 0) last_unicast_ok_ms_ = millis() | 1u;
  } else { ++cb_fail_; note(rf_cb_fail); }
}
bool Link::evidence() {
  const uint32_t ok = last_unicast_ok_ms_;
  return ok != 0 && uint32_t(millis() - ok) < 4000;
}
void Link::rf_reset() {
  tx_accepted_ = tx_refused_ = cb_ok_ = cb_fail_ = rx_frames_ = 0;
  for (auto& n : tx_by_channel_) n = 0;
  for (auto& n : rx_by_channel_) n = 0;
}
void Link::rf_report() const {
  Serial.printf("ORCDIAL_RF ch=%u tx=%lu tx_refused=%lu cb_ok=%lu cb_fail=%lu rx=%lu\n", unsigned(channel_),
                (unsigned long)tx_accepted_, (unsigned long)tx_refused_, (unsigned long)cb_ok_,
                (unsigned long)cb_fail_, (unsigned long)rx_frames_);
  Serial.print("ORCDIAL_RF_BY_CHANNEL");
  for (uint8_t c = 1; c <= 13; ++c)
    Serial.printf(" %u:tx%lu/rx%lu", unsigned(c), (unsigned long)tx_by_channel_[c], (unsigned long)rx_by_channel_[c]);
  Serial.print(" set_ch_err=");Serial.print((unsigned long)set_channel_errors_);
  Serial.println();
}

bool Link::begin() {
  WiFi.mode(WIFI_STA); WiFi.disconnect(false,false);
  if(esp_now_init()!=ESP_OK)return false;
  uint8_t mac[6];esp_wifi_get_mac(WIFI_IF_STA,mac);device_id_=esp_random();
  Preferences saved;saved.begin("orcdial",true);const bool legacy=saved.getBytesLength("peer")==6;saved_channel_=saved.getUChar("pref_ch",0);saved.end();
  uint8_t first_channel=1;wifi_second_chan_t secondary;
  if(esp_wifi_get_channel(&first_channel,&secondary)==ESP_OK)channel_=first_channel;
  active_=this;esp_now_register_recv_cb(receive);esp_now_register_send_cb(sent);
  secure_.preferred_channel(saved_channel_);
  if(!secure_.begin(1,mac,legacy,transmit,scan,evidence)) {
    // Do not leave ESP-NOW initialized with live callbacks around a runtime that failed to start.
    esp_now_unregister_recv_cb();esp_now_unregister_send_cb();esp_now_deinit();active_=nullptr;
    return false;
  }
  secure_.enabled(true);Serial.println("ESPNOW_V4_INIT_OK");return true;
}
void Link::receive(const uint8_t* mac,const uint8_t* data,int size) {
  if(active_ && size==64) {
    ++rx_frames_;if(channel_<14)++rx_by_channel_[channel_];note(rf_rx);
    active_->secure_.receive(mac,data,64);
  }
}
bool Link::transmit(const uint8_t* mac,const uint8_t* wire) {
  if(!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t info{};std::memcpy(info.peer_addr,mac,6);info.ifidx=WIFI_IF_STA;
    if(esp_now_add_peer(&info)!=ESP_OK)return false;
  }
  const bool accepted=esp_now_send(mac,wire,64)==ESP_OK;
  if(accepted){++tx_accepted_;if(channel_<14)++tx_by_channel_[channel_];note(rf_tx_accepted);}
  else{++tx_refused_;note(rf_tx_refused);}
  return accepted;
}
void Link::scan(uint8_t channel){
  // Trust the radio, not the request: record the channel it actually reports after the change.
  if(esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE)!=ESP_OK)++set_channel_errors_;
  uint8_t actual=channel;wifi_second_chan_t secondary;
  channel_=esp_wifi_get_channel(&actual,&secondary)==ESP_OK?actual:channel;
  note(rf_scan);
}
bool Link::send(Type type, int32_t value, uint32_t sequence, ActionKind action) {
  Packet p;
  p.type = type; p.role = Role::dial; p.sender = device_id_;
  p.sequence = sequence ? sequence : next_sequence_++;
  p.value = value;
  p.frequency_hz = state_.frequency_hz; p.step_hz = state_.step_hz;
  p.dashboard = uint8_t(state_.dashboard);
  p.action = uint8_t(action); p.view = state_.view;
  uint8_t data[packet_size]; encode(p, data);
  const bool sent = secure_.send(data,packet_size);
  if (sent) last_tx_ms_ = millis();
  if (sent &&
      type != Type::hello && type != Type::heartbeat && type != Type::pair_request) {
    pending_ = p; pending_sequence_ = p.sequence; pending_ms_ = millis(); retries_ = 0;
    Serial.printf("TX seq=%lu type=%u value=%ld\n", (unsigned long)p.sequence, unsigned(type), long(value));
  }
  return sent;
}
void Link::start_pairing() {secure_.action(secure::Action::pair);pending_sequence_=0;}
void Link::forget_and_pair() {secure_.action(secure::Action::forget_repair);pending_sequence_=0;}
void Link::disconnect(bool) {secure_.action(secure::Action::disconnect);pending_sequence_=0;connected_=false;}
bool Link::command(Type type, int32_t value) {
  if (!connected() || pending_sequence_) return false;
  return send(type, value);
}
bool Link::command_action(Action action) {
  if (!connected() || pending_sequence_ || action.kind == ActionKind::none) return false;
  return send(Type::semantic_action, action.value, 0, action.kind);
}
void Link::handle(const Incoming& incoming) {
  Packet p;
  if (!decode(incoming.data, incoming.size, p) || p.role != Role::receiver) return;
  last_rx_ms_ = millis();
  if (p.type == Type::radio_state) {
    if (p.frequency_hz < 24000 || p.frequency_hz > 1766000000 || !valid_dashboard(p.dashboard)) return;
    if (p.sender != last_sender_) { last_sender_ = p.sender; last_state_sequence_ = 0; }
    if (!newer_sequence(p.sequence, last_state_sequence_)) return;
    last_state_sequence_ = p.sequence;
    connected_ = true;
    state_.frequency_hz = p.frequency_hz; state_.step_hz = p.step_hz;
    state_.gain_tenth_db = p.gain_tenth_db; state_.squelch = p.squelch;
    state_.signal_dbm = p.signal_dbm; state_.signal_valid = p.flags & 1;
    state_.mode = p.mode; state_.volume = p.volume;
    state_.dashboard = Dashboard(p.dashboard);
    state_.view = p.view; state_.revision = p.revision;
    state_.selected = p.selected; state_.item_count = p.item_count;
    state_.capabilities = p.capabilities;
    if (pending_sequence_ && p.ack == pending_sequence_) { last_ack_ = p.ack; pending_sequence_ = 0; }
    Serial.printf("RX seq=%lu type=RADIO_STATE freq=%lu\n", (unsigned long)p.sequence, (unsigned long)p.frequency_hz);
  } else if (p.type == Type::error && pending_sequence_ && p.ack == pending_sequence_) {
    Serial.printf("ACTION_REJECTED seq=%lu reason=%ld\n", (unsigned long)p.ack, long(p.value));
    pending_sequence_ = 0;
  }
}
void Link::poll() {
  // Print queued radio events from the main task; radio callbacks never touch Serial.
  const bool tracing = trace_mode_ == 1 || (trace_mode_ == 2 && millis() < 60000);
  for (;;) {
    RfEvent e;
    portENTER_CRITICAL(&ring_lock_);
    const bool have = ring_tail_ != ring_head_;
    if (have) { e = ring_[ring_tail_ % rf_ring_size]; ++ring_tail_; }
    portEXIT_CRITICAL(&ring_lock_);
    if (!have) break;
    if (tracing) Serial.printf("RF t=%lu ev=%s ch=%u\n", (unsigned long)e.ms, rf_names[e.kind], unsigned(e.channel));
  }
  const auto status=secure_.status();paused_=status.state==secure::State::paused;
  if(status.state!=secure::State::connected){connected_=false;pending_sequence_=0;last_state_sequence_=0;connected_at_=0;return;}
  // Remember the channel a working connection used, so the next boot looks there first.
  if(!connected_at_)connected_at_=millis()|1u;
  else if(channel_>=1 && channel_<=11 && channel_!=saved_channel_ && uint32_t(millis()-connected_at_)>5000) {
    Preferences saved;saved.begin("orcdial",false);saved.putUChar("pref_ch",channel_);saved.end();
    saved_channel_=channel_;secure_.preferred_channel(channel_);
    Serial.printf("ORCDIAL_PREF_CHANNEL saved=%u\n",unsigned(channel_));
  }
  secure::Runtime::Control c{};
  while(secure_.take(c)){if(c.size!=packet_size)continue;Incoming in{};in.size=c.size;std::memcpy(in.data,c.data,c.size);handle(in);}
  const uint32_t now=millis();
  if(pending_sequence_&&now-pending_ms_>650){
    if(++retries_>2){Serial.println("ACK_TIMEOUT");pending_sequence_=0;}
    else{uint8_t data[packet_size];encode(pending_,data);secure_.send(data,sizeof data);pending_ms_=now;}
  }
  if(!connected_&&now-last_tx_ms_>500)send(Type::request_state);
  else if(connected_&&now-last_tx_ms_>1000)send(Type::heartbeat);
}
} // namespace orc

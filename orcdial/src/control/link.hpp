#pragma once
#include "protocol.hpp"
#include "../state.hpp"
#include "../controller.hpp"
#include <Arduino.h>
#include <esp_now.h>
#include <freertos/queue.h>

namespace orc {
class Link {
 public:
  bool begin();
  void poll();
  void start_pairing();
  bool command(Type type, int32_t value);
  bool command_action(Action action);
  bool connected() const { return connected_; }
  bool pairing() const { return pairing_; }
  bool pending() const { return pending_sequence_ != 0; }
  uint32_t pending_sequence() const { return pending_sequence_; }
  const RadioState& state() const { return state_; }
  uint32_t last_ack() const { return last_ack_; }
 private:
  struct Incoming { uint8_t mac[6]; uint8_t data[packet_size]; uint8_t size; };
  static void receive(const uint8_t* mac, const uint8_t* data, int size);
  void handle(const Incoming& incoming);
  bool send(Type type, int32_t value = 0, uint32_t sequence = 0, ActionKind action = ActionKind::none);
  void peer(const uint8_t* mac);
  QueueHandle_t queue_ = nullptr;
  uint8_t peer_mac_[6]{};
  uint32_t device_id_ = 0;
  uint32_t next_sequence_ = 1;
  uint32_t pending_sequence_ = 0;
  uint32_t last_ack_ = 0;
  uint32_t last_sender_ = 0;
  uint32_t last_state_sequence_ = 0;
  uint32_t last_rx_ms_ = 0;
  uint32_t last_tx_ms_ = 0;
  uint32_t pending_ms_ = 0;
  uint32_t scan_ms_ = 0;
  uint8_t scan_channel_ = 1;
  uint8_t retries_ = 0;
  bool has_peer_ = false;
  bool connected_ = false;
  bool pairing_ = false;
  Packet pending_{};
  RadioState state_{};
  static Link* active_;
};
} // namespace orc
